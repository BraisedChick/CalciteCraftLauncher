#pragma once

// 区块网格调度器（图形 API 无关的公共组件，OpenGL / Vulkan 后端共用）
// 职责：脏 section 标记、新区块发现与卸载调度、worker 线程池离线网格生成、
//       顶点压缩与 section 连通性计算。
// 产出 ChunkMeshResult / 待删除 chunkKey，由渲染后端在各自渲染线程
// poll 后完成 GPU 资源的上传与删除（本组件不接触任何图形 API）。

#include <cstdint>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <unordered_set>
#include <unordered_map>

class ChunkManager;

// ===== 压缩顶点格式（GPU 上传用，48→32 bytes，后端无关）=====
struct PackedVertex {
    float    pos[3];      //  0: 12 bytes (FLOAT, 世界坐标，不压缩)
    float    texIndex;    // 12:  4 bytes (FLOAT)
    uint8_t  color[4];    // 16:  4 bytes (UNSIGNED_BYTE, 归一化)
    uint16_t uv[2];       // 20:  4 bytes (UNSIGNED_SHORT, 归一化 [0,1]→[0,65535])
    uint16_t uv2[2];      // 24:  4 bytes (UNSIGNED_SHORT, 归一化 [0,1]→[0,65535])
    int8_t   normal[4];   // 28:  4 bytes (BYTE, 归一化 [-1,1]→[-127,127], w未用)
}; // Total: 32 bytes

// ===== 网格生成结果（worker → 渲染后端）=====
struct ChunkMeshResult {
    uint64_t chunkKey;
    uint64_t sectionMask = ~0ULL;  // 本次重建覆盖的 section 掩码（掩码内旧资源需替换/移除）
    // 每个 section 独立数据，不合并
    struct SectionData {
        int sectionY;
        std::vector<PackedVertex> packedVertices;  // 在工作线程压缩好，渲染线程直接上传
        std::vector<uint32_t> baseIndices;
        std::vector<uint32_t> overlayIndices;
        std::vector<uint32_t> waterIndices;
        uint64_t visibilityData = 0;  // 该 section 的方向连通性数据
    };
    std::vector<SectionData> sections;
};

class ChunkMeshScheduler {
public:
    ChunkMeshScheduler();   // 启动 worker 线程池
    ~ChunkMeshScheduler();  // 停止并 join 全部 worker

    // 设置 ChunkManager（原子，跨线程安全）；非空时触发一轮全量调度
    void setChunkManager(ChunkManager* manager);

    // 标记整柱需要重建（等价于 sectionMask=~0ULL）
    void markChunkForUpdate(int chunkX, int chunkZ);

    // 标记区块内指定 section 需要重建（section 级脏粒度）
    // 掩码 bit = (sectionY >> 4) & 63（section 坐标模 64，无需知道维度 minY）
    void markSectionsForUpdate(int chunkX, int chunkZ, uint64_t sectionMask);

    // 移除单个区块（服务端 ForgetLevelChunk，线程安全）
    // 后端通过 pollRemovals 领取待删除 key，在渲染线程释放 GPU 资源
    void removeChunk(int chunkX, int chunkZ);

    // 清空全部调度状态并重启 worker（断连/重生清屏时由渲染线程调用）
    void clearAll();

    // 每帧调度：消化脏标记 + 发现新区块并派发 worker（渲染线程调用）
    // maxDistance：调度距离上限（一般传渲染距离 farPlane）
    void update(float cameraX, float cameraY, float cameraZ, float maxDistance);

    // 领取已完成的网格结果，最多 maxChunks 个（渲染线程调用），返回实际领取数
    // 已被移除/未知区块的过期结果自动丢弃，不计入配额（防止孤儿渲染数据）
    int pollResults(std::vector<ChunkMeshResult>& out, int maxChunks);

    // 领取待删除区块 key（渲染线程调用），有内容返回 true
    bool pollRemovals(std::vector<uint64_t>& outKeys);

private:
    // ===== 工作线程任务（离线网格生成）=====
    struct ChunkWorkItem {
        uint64_t chunkKey;
        int chunkX;
        int chunkZ;
        float distance;  // 距离玩家距离，优先级队列排序用
        uint64_t sectionMask = ~0ULL;  // 需要重建的 section 掩码，~0ULL=整柱

        bool operator<(const ChunkWorkItem& other) const {
            return distance > other.distance;  // 小顶堆：距离近的优先级高
        }
    };

    void workerLoop();
    void startWorker();
    void stopWorker();
    void enqueueWork(ChunkWorkItem item);

    static uint64_t makeChunkKey(int chunkX, int chunkZ) {
        return ((uint64_t)(chunkX & 0xFFFFFFFF) << 32) | (uint32_t)(chunkZ & 0xFFFFFFFF);
    }

    std::atomic<ChunkManager*> chunkManager{nullptr};
    std::atomic<bool> needRebuildMesh{false};  // 标记是否需要执行一轮调度

    // ===== 调度状态（stateMutex 保护）=====
    std::mutex stateMutex;
    // 脏 section 集合：chunkKey → section 掩码（只重建需要更新的部分，避免整柱重建）
    std::unordered_map<uint64_t, uint64_t> dirtyChunks;
    // 已调度过的区块集合（替代后端渲染缓存的存在性检查；发现阶段跳过已知区块）
    std::unordered_set<uint64_t> knownChunks;
    // 待卸载区块集合（ForgetLevelChunk，后端渲染线程消费）
    std::unordered_set<uint64_t> chunksToRemove;
    size_t lastChunkCount = 0;  // 上次已知区块数，用于发现新区块

    // ===== 去重：已在工作队列/生成中的区块（pendingMutex 保护）=====
    std::mutex pendingMutex;
    std::unordered_set<uint64_t> pendingChunks;

    // ===== 工作线程池（workMutex 保护队列与运行标志）=====
    static constexpr int WORKER_THREAD_COUNT = 4;
    std::vector<std::thread> workerThreads;
    std::mutex workMutex;
    std::condition_variable workCV;
    std::priority_queue<ChunkWorkItem> workQueue;
    bool workerRunning = false;

    // ===== 完成结果（resultMutex 保护，含分帧消费的积压队列）=====
    std::mutex resultMutex;
    std::queue<ChunkMeshResult> resultQueue;     // worker 写入
    std::queue<ChunkMeshResult> pendingResults;  // pollResults 分帧领取的积压
};
