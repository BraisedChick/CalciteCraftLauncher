#include "ChunkMeshScheduler.h"
#include <android/log.h>
#include <cmath>
#include <cstring>
#include "ChunkManager.h"
#include "MeshGenerator.h"
#include "BlockRegistry.h"
#include "ClientEngine/ClientEngine.h"

#define LOG_TAG "ChunkMeshScheduler"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

ChunkMeshScheduler::ChunkMeshScheduler() {
    startWorker();
}

ChunkMeshScheduler::~ChunkMeshScheduler() {
    stopWorker();
}

void ChunkMeshScheduler::setChunkManager(ChunkManager* manager) {
    chunkManager.store(manager);
    // 非空时触发一轮全量调度（新区块在 update 的发现阶段自动入队）
    if (manager) {
        needRebuildMesh.store(true);
    }
}

void ChunkMeshScheduler::markChunkForUpdate(int chunkX, int chunkZ) {
    markSectionsForUpdate(chunkX, chunkZ, ~0ULL);
}

void ChunkMeshScheduler::markSectionsForUpdate(int chunkX, int chunkZ, uint64_t sectionMask) {
    if (sectionMask == 0) return;
    uint64_t chunkKey = makeChunkKey(chunkX, chunkZ);
    std::lock_guard<std::mutex> lock(stateMutex);
    dirtyChunks[chunkKey] |= sectionMask;
    // 与 update() 末尾的 moreWork 判定同锁序执行，防止标脏与置 false 竞态丢标记
    needRebuildMesh.store(true);
}

void ChunkMeshScheduler::removeChunk(int chunkX, int chunkZ) {
    uint64_t chunkKey = makeChunkKey(chunkX, chunkZ);
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        chunksToRemove.insert(chunkKey);
        dirtyChunks.erase(chunkKey);   // 取消排队中的更新
        knownChunks.erase(chunkKey);   // 服务端重发同一区块时允许重新发现
    }
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingChunks.erase(chunkKey);
    }
}

void ChunkMeshScheduler::clearAll() {
    LOGI("clearAll: Clearing all scheduling state...");

    // 1. 停止所有工作线程
    stopWorker();

    // 2. 清空所有队列与调度状态
    {
        std::lock_guard<std::mutex> lock(workMutex);
        while (!workQueue.empty()) workQueue.pop();
    }
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        while (!resultQueue.empty()) resultQueue.pop();
        while (!pendingResults.empty()) pendingResults.pop();
    }
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pendingChunks.clear();
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        dirtyChunks.clear();
        knownChunks.clear();
        chunksToRemove.clear();
        lastChunkCount = 0;
    }

    needRebuildMesh.store(false);

    // 3. 重新启动工作线程
    startWorker();

    LOGI("clearAll: All scheduling state cleared");
}

void ChunkMeshScheduler::update(float cameraX, float cameraY, float cameraZ, float maxDistance) {
    (void)cameraY;  // 距离剔除只看水平面，保留参数供未来垂直调度使用
    if (!needRebuildMesh.load()) return;

    auto* mgr = chunkManager.load();
    if (!mgr) {
        needRebuildMesh.store(false);
        return;
    }

    int chunksEnqueued = 0;
    bool anyDeferred = false;  // pending 中收到新脏标记，需下一帧重试

    // ===== 第一步：处理脏 section（由 markSectionsForUpdate 标记的）=====
    std::unordered_map<uint64_t, uint64_t> localDirty;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        localDirty.swap(dirtyChunks);
    }

    for (const auto& [chunkKey, sectionMask] : localDirty) {
        int chunkX = (int)(chunkKey >> 32);
        int chunkZ = (int)(chunkKey & 0xFFFFFFFF);

        auto chunk = mgr->getChunk(chunkX, chunkZ);
        if (!chunk || !chunk->isLoaded) continue;

        // 距离计算
        float chunkCenterX = chunk->pos.x * 16.0f + 8.0f;
        float chunkCenterZ = chunk->pos.z * 16.0f + 8.0f;
        float distX = chunkCenterX - cameraX;
        float distZ = chunkCenterZ - cameraZ;
        float distance = sqrtf(distX * distX + distZ * distZ);

        // 超出调度距离直接丢弃脏标记（不入 knownChunks，等靠近后由发现阶段重新调度）
        if (distance > maxDistance) continue;

        {
            std::lock_guard<std::mutex> lock(stateMutex);
            knownChunks.insert(chunkKey);
        }

        // 入队重建；若已在工作队列中则把掩码合并回脏集合，下一帧重试（避免丢更新）
        bool doEnqueue = false;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            if (pendingChunks.find(chunkKey) == pendingChunks.end()) {
                pendingChunks.insert(chunkKey);
                doEnqueue = true;
            }
        }
        if (doEnqueue) {
            enqueueWork({chunkKey, chunk->pos.x, chunk->pos.z, distance, sectionMask});
            chunksEnqueued++;
        } else {
            std::lock_guard<std::mutex> lock(stateMutex);
            dirtyChunks[chunkKey] |= sectionMask;
            anyDeferred = true;
        }
    }

    // ===== 第二步：发现新区块（仅在区块数量变化时扫描）=====
    size_t currentCount = mgr->getLoadedChunkCount();
    bool scanAll;
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        scanAll = (currentCount != lastChunkCount);
        if (scanAll) lastChunkCount = currentCount;
    }
    if (scanAll) {
        auto allChunks = mgr->getAllChunks();

        for (const auto& chunk : allChunks) {
            if (!chunk || !chunk->isLoaded) continue;

            uint64_t chunkKey = makeChunkKey(chunk->pos.x, chunk->pos.z);

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                if (knownChunks.find(chunkKey) != knownChunks.end()) continue;
            }

            // 距离计算
            float chunkCenterX = chunk->pos.x * 16.0f + 8.0f;
            float chunkCenterZ = chunk->pos.z * 16.0f + 8.0f;
            float distX = chunkCenterX - cameraX;
            float distZ = chunkCenterZ - cameraZ;
            float distance = sqrtf(distX * distX + distZ * distZ);
            if (distance > maxDistance) continue;

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                knownChunks.insert(chunkKey);
            }

            // 新区块立即入队网格生成（整柱）
            {
                std::lock_guard<std::mutex> lock(pendingMutex);
                if (pendingChunks.find(chunkKey) != pendingChunks.end()) continue;
                pendingChunks.insert(chunkKey);
            }
            enqueueWork({chunkKey, chunk->pos.x, chunk->pos.z, distance});
            chunksEnqueued++;
        }
    }

    if (chunksEnqueued > 0) {
        LOGI("update: enqueued %d dirty chunks", chunksEnqueued);
    }

    // 有延迟的脏标记或调度期间新到的脏标记时，下一帧继续轮询
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        needRebuildMesh.store(anyDeferred || !dirtyChunks.empty());
    }
}

int ChunkMeshScheduler::pollResults(std::vector<ChunkMeshResult>& out, int maxChunks) {
    // 将新完成的结果追加到积压队列（分帧消费，避免一帧内上传大量 GPU 资源导致卡顿）
    {
        std::lock_guard<std::mutex> lock(resultMutex);
        while (!resultQueue.empty()) {
            pendingResults.push(std::move(resultQueue.front()));
            resultQueue.pop();
        }
    }

    int taken = 0;
    while (taken < maxChunks) {
        ChunkMeshResult result;
        {
            std::lock_guard<std::mutex> lock(resultMutex);
            if (pendingResults.empty()) break;
            result = std::move(pendingResults.front());
            pendingResults.pop();
        }

        // 无条件从 pending 清除（该区块之后可以再次入队）
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingChunks.erase(result.chunkKey);
        }

        // 已被移除/未知区块的过期结果直接丢弃（不计入配额），防止孤儿渲染数据复活
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (knownChunks.find(result.chunkKey) == knownChunks.end()) continue;
        }

        out.push_back(std::move(result));
        taken++;
    }
    return taken;
}

bool ChunkMeshScheduler::pollRemovals(std::vector<uint64_t>& outKeys) {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (chunksToRemove.empty()) return false;
    outKeys.assign(chunksToRemove.begin(), chunksToRemove.end());
    chunksToRemove.clear();
    return true;
}

// ===== 工作线程：离线网格生成，不阻塞渲染线程 =====

void ChunkMeshScheduler::startWorker() {
    workerRunning = true;
    for (int i = 0; i < WORKER_THREAD_COUNT; i++) {
        workerThreads.emplace_back(&ChunkMeshScheduler::workerLoop, this);
    }
    LOGI("Started %d mesh worker threads", WORKER_THREAD_COUNT);
}

void ChunkMeshScheduler::stopWorker() {
    {
        std::lock_guard<std::mutex> lock(workMutex);
        workerRunning = false;
    }
    workCV.notify_all();
    for (auto& t : workerThreads) {
        if (t.joinable()) {
            t.join();
        }
    }
    workerThreads.clear();
}

void ChunkMeshScheduler::enqueueWork(ChunkWorkItem item) {
    {
        std::lock_guard<std::mutex> lock(workMutex);
        workQueue.push(std::move(item));
    }
    workCV.notify_one();
}

// ===== Sodium 风格遮挡剔除：预计算 section 内部连通性 =====
// 方向: 0=DOWN 1=UP 2=NORTH 3=SOUTH 4=WEST 5=EAST
// 结果: bit(from*8+to)=1 表示 from 面→to 面在 section 内部可达
static const int FACE_AXIS[6] = {1,1,2,2,0,0};
static const int FACE_VAL[6]  = {0,15,0,15,0,15};

static uint64_t computeSectionVisibility(const ChunkSection& section) {
    auto toIdx = [](int x,int y,int z){ return (y<<8)|(z<<4)|x; };
    bool isSolid[16*16*16]={false};
    int solidCount=0;
    for(int y=0;y<16;y++) for(int z=0;z<16;z++) for(int x=0;x<16;x++){
        int idx=toIdx(x,y,z);
        int32_t st=section.blockStates[idx];
        if(!st) continue;
        auto& meta=ClientEngine::getInstance()->getBlockRegistry()->getBlockMetadata(st);
        if(meta.isFullBlock&&meta.isOpaque) { isSolid[idx]=true; solidCount++; }
    }
    if(solidCount==4096) return 0;
    if(solidCount<256) return ~0ULL;
    static const int NB[6][3]={{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
    uint64_t vis=0;
    int queue[4096];
    for(int of=0;of<6;of++){
        bool visited[4096]={false};
        int qH=0,qT=0;
        int fix=FACE_AXIS[of],val=FACE_VAL[of],a1=(fix+1)%3,a2=(fix+2)%3;
        for(int d1=0;d1<16;d1++) for(int d2=0;d2<16;d2++){
            int c[3]; c[fix]=val; c[a1]=d1; c[a2]=d2;
            int idx=toIdx(c[0],c[1],c[2]);
            if(!isSolid[idx]&&!visited[idx]){ visited[idx]=true; queue[qT++]=idx; }
        }
        while(qH<qT){
            int cur=queue[qH++],cx=cur&0xF,cy=(cur>>8)&0xF,cz=(cur>>4)&0xF;
            for(int d=0;d<6;d++){
                int nx=cx+NB[d][0],ny=cy+NB[d][1],nz=cz+NB[d][2];
                if((unsigned)nx>=16||(unsigned)ny>=16||(unsigned)nz>=16) continue;
                int ni=toIdx(nx,ny,nz);
                if(!isSolid[ni]&&!visited[ni]){ visited[ni]=true; queue[qT++]=ni; }
            }
        }
        vis|=1ULL<<(of*8+of);
        for(int tf=0;tf<6;tf++){
            if(tf==of) continue;
            int tfx=FACE_AXIS[tf],tfv=FACE_VAL[tf],ta1=(tfx+1)%3,ta2=(tfx+2)%3;
            bool ok=false;
            for(int d1=0;d1<16&&!ok;d1++) for(int d2=0;d2<16&&!ok;d2++){
                int c[3]; c[tfx]=tfv; c[ta1]=d1; c[ta2]=d2;
                if(visited[toIdx(c[0],c[1],c[2])]) ok=true;
            }
            if(ok) vis|=1ULL<<(of*8+tf);
        }
    }
    return vis;
}

void ChunkMeshScheduler::workerLoop() {
    LOGI("Mesh worker thread started");

    // 线程局部的 scratch vectors，跨 section/chunk 复用避免反复分配
    std::vector<Vertex> wl_baseVertices, wl_overlayVertices, wl_waterVertices;
    std::vector<uint32_t> wl_baseIndices, wl_overlayIndices, wl_waterIndices;

    while (workerRunning) {
        ChunkWorkItem item;
        {
            std::unique_lock<std::mutex> lock(workMutex);
            workCV.wait(lock, [this]() {
                return !workQueue.empty() || !workerRunning;
            });

            if (!workerRunning) break;

            item = workQueue.top();
            workQueue.pop();
        }

        // 在工作线程中生成网格（CPU 密集，不涉及任何图形 API）
        // shared_ptr 确保区块在工作期间不会被网络线程卸载
        auto* mgr = chunkManager.load();
        auto chunk = mgr ? mgr->getChunk(item.chunkX, item.chunkZ) : nullptr;
        if (!chunk || !chunk->isLoaded) {
            // 区块不存在，从 pending 中移除
            std::lock_guard<std::mutex> lock(pendingMutex);
            pendingChunks.erase(item.chunkKey);
            continue;
        }

        ChunkMeshResult result;
        result.chunkKey = item.chunkKey;
        result.sectionMask = item.sectionMask;

        for (size_t sectionIdx = 0; sectionIdx < chunk->sections.size(); ++sectionIdx) {
            const auto& section = chunk->sections[sectionIdx];
            if (!section || section->isEmpty) continue;
            // section 级脏粒度：只重建掩码内的 section，其余保留旧 GPU 资源
            if (!(item.sectionMask & (1ULL << ((section->y >> 4) & 63)))) continue;

            auto meshOut = MeshGenerator::generateSectionMesh(
                *section, item.chunkX, section->y, item.chunkZ, mgr,
                wl_baseVertices, wl_baseIndices,
                wl_overlayVertices, wl_overlayIndices,
                wl_waterVertices, wl_waterIndices);

            if (meshOut.vertices.empty()) continue;

            ChunkMeshResult::SectionData secData;
            secData.sectionY = section->y;
            secData.visibilityData = computeSectionVisibility(*section);

            // 在工作线程压缩 Vertex（48B）→ PackedVertex（32B），减轻渲染线程负担
            auto& srcVerts = meshOut.vertices;
            secData.packedVertices.resize(srcVerts.size());
            for (size_t vi = 0; vi < srcVerts.size(); vi++) {
                const auto& src = srcVerts[vi];
                auto& dst = secData.packedVertices[vi];
                // pos: 世界坐标，float 不压缩，无精度损失
                memcpy(dst.pos, src.pos, sizeof(float) * 3);
                dst.texIndex = src.texIndex;
                memcpy(dst.color, src.color, 4);
                // uv: [0,1] → [0,65535]
                dst.uv[0] = (uint16_t)(src.texCoord[0] * 65535.0f + 0.5f);
                dst.uv[1] = (uint16_t)(src.texCoord[1] * 65535.0f + 0.5f);
                // normal: [-1,1] → [-127,127]
                dst.normal[0] = (int8_t)(src.normal[0] * 127.0f);
                dst.normal[1] = (int8_t)(src.normal[1] * 127.0f);
                dst.normal[2] = (int8_t)(src.normal[2] * 127.0f);
                dst.normal[3] = 0;
                // uv2: lightmap [0,240] → [0,65535]
                dst.uv2[0] = (uint16_t)(src.uv2[0] + 0.5f);
                dst.uv2[1] = (uint16_t)(src.uv2[1] + 0.5f);
            }

            size_t regularCount = meshOut.indices.size()
                - meshOut.overlayIndexCount - meshOut.waterIndexCount;

            secData.baseIndices.assign(
                meshOut.indices.begin(),
                meshOut.indices.begin() + regularCount);

            size_t ovStart = regularCount;
            if (meshOut.overlayIndexCount > 0) {
                secData.overlayIndices.assign(
                    meshOut.indices.begin() + ovStart,
                    meshOut.indices.begin() + ovStart + meshOut.overlayIndexCount);
            }

            size_t watStart = ovStart + meshOut.overlayIndexCount;
            if (meshOut.waterIndexCount > 0) {
                secData.waterIndices.assign(
                    meshOut.indices.begin() + watStart,
                    meshOut.indices.end());
            }

            result.sections.push_back(std::move(secData));
        }

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            resultQueue.push(std::move(result));
        }
    }

    LOGI("Mesh worker thread stopped");
}
