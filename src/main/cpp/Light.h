#pragma once

#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <unordered_set>
#include <vector>
#include <utility>

class ChunkManager;

/// 光照系统管理（纯逻辑，不碰图形 API）：光照贴图像素生成 + 昼夜循环 + 天空颜色 + 方块光传播
/// 纹理创建/上传由 GLRenderer / VulkanRenderer 各自负责
class Light {
public:
    Light();
    ~Light();

    // ===== 光照贴图像素（渲染后端无关）=====
    /// 每帧调用：更新天空/雾效颜色
    void update();

    /// 天空亮度变化超过阈值时生成 16x16 RGBA 像素并返回 true（首次调用必返回 true）
    /// 调用方拿到像素后自行上传纹理（GL: glTexSubImage2D / Vulkan: staging buffer）
    bool getLightmapPixelsIfChanged(uint8_t* pixels);

    /// 使像素缓存失效：渲染器（重）建纹理后调用，保证下次 poll 必返回新像素
    void invalidateLightmapCache() { lastGeneratedSkyBright = -1.0f; }

    // ===== 昼夜循环 =====
    void setWorldDayTime(long long dayTime) { worldDayTime = dayTime; }
    long long getWorldDayTime() const { return worldDayTime; }
    float getSkyDarken() const;

    // ===== 天空/雾效颜色 =====
    float getSkyColorR() const { return skyR; }
    float getSkyColorG() const { return skyG; }
    float getSkyColorB() const { return skyB; }

    // ===== 客户端光照增量传播（双队列算法，方块光 + 天空光）=====
    /// 异步入队：网络线程调用，不阻塞（方块变化时对该坐标同时触发方块光+天空光增量更新）
    void queueLightRecalc(int worldX, int worldY, int worldZ);

    /// 取走光照更新波及的脏 chunk 列表（渲染线程调用，用于精准 remesh）
    bool pollDirtyLightChunks(std::vector<std::pair<int, int>>& outChunks);

    /// 根据方块名称获取发光等级（0-15），仅供 BlockRegistry 加载时预计算 emission
    static int getBlockEmission(const char* blockName);

    /// 设置 ChunkManager 引用（后台线程用）
    void setChunkManager(ChunkManager* cm) { chunkManagerRef = cm; }

private:
    void startWorkerThread();
    void workerLoop();

    /// 增量光照更新：checkNode 收集 + 变暗/变亮两阶段 FIFO 传播，方块光与天空光各跑一遍（工作线程执行）
    void runLightUpdates(ChunkManager* chunkMgr, const std::vector<uint64_t>& nodes);

    long long worldDayTime = 6000;
    float skyR = 0.53f, skyG = 0.81f, skyB = 0.92f;
    float lastGeneratedSkyBright = -1.0f;  // 上次生成光照贴图像素时的天空亮度
    void generateLightmapPixels(float skyBright, uint8_t* pixels);

    // 异步光照重算
    ChunkManager* chunkManagerRef = nullptr;
    std::thread workerThread;
    std::atomic<bool> workerRunning{false};

    // 输入队列（网络线程 → 工作线程，去重集合，元素为打包坐标）
    std::mutex inputMutex;
    std::condition_variable inputCV;
    std::unordered_set<uint64_t> pendingNodes;

    // 输出（工作线程 → 渲染线程）：本轮传播波及的脏 chunk key 集合
    std::mutex outputMutex;
    std::unordered_set<uint64_t> dirtyChunkKeys;
};
