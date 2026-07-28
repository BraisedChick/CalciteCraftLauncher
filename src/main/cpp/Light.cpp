#include "Light.h"
#include "ChunkManager.h"
#include "BlockRegistry.h"
#include "ClientEngine/ClientEngine.h"
#include <android/log.h>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <cstring>
#include <string>

#define LOG_TAG "Light"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ===== 昼夜计算 =====

float Light::getSkyDarken() const {
    // DayTime: 0=sunrise, 6000=noon, 12000=sunset, 18000=midnight, 24000=sunrise
    long long dt = worldDayTime % 24000;
    if (dt < 0) dt += 24000;

    float darken;
    if (dt < 12000) {
        darken = 0.0f;                        // 白天
    } else if (dt < 13000) {
        darken = (float)(dt - 12000) / 1000.0f; // 黄昏
    } else if (dt < 23000) {
        darken = 1.0f;                         // 夜晚
    } else {
        darken = 1.0f - (float)(dt - 23000) / 1000.0f; // 黎明
    }
    return darken;
}

// ===== 光照贴图像素输出（渲染后端无关，纹理上传由调用方负责）=====

bool Light::getLightmapPixelsIfChanged(uint8_t* pixels) {
    float skyBright = 1.0f - getSkyDarken();

    // 仅在天空亮度变化超过阈值时重新生成（避免每帧上传）
    if (fabsf(skyBright - lastGeneratedSkyBright) <= 0.001f) return false;

    generateLightmapPixels(skyBright, pixels);
    lastGeneratedSkyBright = skyBright;
    return true;
}

// ===== 每帧更新（天空/雾效颜色）=====

void Light::update() {
    float skyDarken = getSkyDarken();
    float skyBright = 1.0f - skyDarken;

    // 天空/雾效颜色插值：白天蓝 → 夜晚深蓝黑（每帧更新，开销极小）
    skyR = 0.53f * skyBright + 0.02f * skyDarken;
    skyG = 0.81f * skyBright + 0.02f * skyDarken;
    skyB = 0.92f * skyBright + 0.08f * skyDarken;
}

// ===== 光照贴图像素生成 =====

void Light::generateLightmapPixels(float skyBright, uint8_t* pixels) {
    for (int sy = 0; sy < 16; sy++) {
        for (int bx = 0; bx < 16; bx++) {
            float blockBright = bx / 15.0f;
            float skyBrightLevel = (sy / 15.0f) * skyBright;

            // 方块光颜色（暖色/橙红——火把/熔岩风格）
            float blockR = blockBright;
            float blockG = blockBright * ((blockBright * 0.6f + 0.4f) * 0.6f + 0.4f);
            float blockB = blockBright * (blockBright * blockBright * 0.6f + 0.4f);

            // 天空光颜色（冷色/蓝白，随昼夜变暗）
            float sR = skyBrightLevel * 0.9f;
            float sG = skyBrightLevel * 1.0f;
            float sB = skyBrightLevel * 1.1f;

            // 合成
            float totalR = fminf(blockR + sR, 1.0f);
            float totalG = fminf(blockG + sG, 1.0f);
            float totalB = fminf(blockB + sB, 1.0f);

            // 混合一点灰色
            totalR = totalR * 0.96f + 0.04f * 0.75f;
            totalG = totalG * 0.96f + 0.04f * 0.75f;
            totalB = totalB * 0.96f + 0.04f * 0.75f;

            // Gamma 校正（原版 notGamma 风格）
            float gamma = 0.5f;
            float ngR = 1.0f - powf(1.0f - totalR, 4.0f);
            float ngG = 1.0f - powf(1.0f - totalG, 4.0f);
            float ngB = 1.0f - powf(1.0f - totalB, 4.0f);
            totalR = totalR * (1.0f - gamma) + ngR * gamma;
            totalG = totalG * (1.0f - gamma) + ngG * gamma;
            totalB = totalB * (1.0f - gamma) + ngB * gamma;

            totalR = fminf(fmaxf(totalR, 0.0f), 1.0f);
            totalG = fminf(fmaxf(totalG, 0.0f), 1.0f);
            totalB = fminf(fmaxf(totalB, 0.0f), 1.0f);

            // 最低环境光保底（避免全黑洞穴完全不可见）
            const float minAmbient = 0.15f;
            totalR = fmaxf(totalR, minAmbient);
            totalG = fmaxf(totalG, minAmbient);
            totalB = fmaxf(totalB, minAmbient);

            int idx = (sy * 16 + bx) * 4;
            pixels[idx + 0] = (uint8_t)(totalR * 255.0f);
            pixels[idx + 1] = (uint8_t)(totalG * 255.0f);
            pixels[idx + 2] = (uint8_t)(totalB * 255.0f);
            pixels[idx + 3] = 255;
        }
    }
}

// ===== 方块发光等级查询 =====

int Light::getBlockEmission(const char* name) {
    if (!name || !*name) return 0;
    std::string n(name);

    // 完整名称匹配（高优先级）
    if (n == "torch" || n == "wall_torch") return 14;
    if (n == "soul_torch" || n == "soul_wall_torch") return 10;
    if (n == "glowstone") return 15;
    if (n == "sea_lantern") return 15;
    if (n == "beacon") return 15;
    if (n == "end_rod") return 14;
    if (n == "redstone_lamp") return 15;
    if (n == "jack_o_lantern") return 15;
    if (n == "shroomlight") return 15;
    if (n == "lantern") return 15;
    if (n == "soul_lantern") return 12;
    if (n == "campfire") return 15;
    if (n == "soul_campfire") return 10;
    if (n == "lava") return 15;
    if (n == "magma_block") return 3;
    if (n == "brewing_stand") return 1;
    if (n == "brown_mushroom") return 1;
    if (n == "ender_chest") return 7;
    if (n == "crying_obsidian") return 10;
    if (n == "respawn_anchor") return 3;
    if (n == "dragon_egg") return 1;
    if (n == "end_portal_frame") return 1;
    if (n == "amethyst_cluster") return 5;
    if (n == "large_amethyst_bud") return 4;
    if (n == "medium_amethyst_bud") return 2;
    if (n == "small_amethyst_bud") return 1;
    if (n == "candle" || n == "white_candle" || n == "orange_candle" ||
        n == "magenta_candle" || n == "light_blue_candle" || n == "yellow_candle" ||
        n == "lime_candle" || n == "pink_candle" || n == "gray_candle" ||
        n == "light_gray_candle" || n == "cyan_candle" || n == "purple_candle" ||
        n == "blue_candle" || n == "brown_candle" || n == "green_candle" ||
        n == "red_candle" || n == "black_candle") return 3;

    // 子串匹配回退
    if (n.find("torch") != std::string::npos) return 14;
    if (n.find("lantern") != std::string::npos) return 15;
    if (n.find("campfire") != std::string::npos) return 15;
    if (n.find("lava") != std::string::npos) return 15;

    return 0;
}

// ===== 客户端光照增量传播（方块光 + 天空光）=====
//
// 核心思路：
// - checkNode 只入队变化点，变暗（decrease）先全部排干，变亮（increase）再排干
// - 队列条目打包：低 4 位光值 + 6 位方向掩码 + 发光标志位，避免向来源方向回传
// - 变暗遇到有其他光源支撑的邻居时，让其单向"补光"，无需区域清除重扫
// - 天空光复用同一算法（processLayer 参数化），服务器快照仅作初始态，增量全靠客户端

namespace {

// --- 坐标打包（x/z 各 26 位，y 12 位，带符号）---
inline uint64_t packPos(int x, int y, int z) {
    return ((uint64_t)((uint32_t)x & 0x3FFFFFFu) << 38)
         | ((uint64_t)((uint32_t)z & 0x3FFFFFFu) << 12)
         | ((uint64_t)((uint32_t)y & 0xFFFu));
}
inline void unpackPos(uint64_t p, int& x, int& y, int& z) {
    x = (int)((int64_t)p >> 38);
    z = (int)(((int64_t)(p << 26)) >> 38);
    y = (int)(((int64_t)(p << 52)) >> 52);
}

// --- 6 方向：成对排列，opposite(d) = d ^ 1 ---
constexpr int DX[6] = {1, -1, 0, 0, 0, 0};
constexpr int DY[6] = {0, 0, 1, -1, 0, 0};
constexpr int DZ[6] = {0, 0, 0, 0, 1, -1};
constexpr int DIR_DOWN = 3;  // DY[3] == -1，天空光垂直下落方向

// --- 队列条目打包（省去形状遮挡标志）---
// bit 0-3: fromLevel；bit 4-9: 方向掩码；bit 10: increase 来自发光源
constexpr uint32_t DIRS_ALL = 0x3Fu << 4;
constexpr uint32_t FLAG_FROM_EMISSION = 1u << 10;

inline uint32_t dirBit(int d) { return 1u << (d + 4); }
inline int entryLevel(uint32_t e) { return (int)(e & 15u); }
inline uint32_t decreaseAllDirections(int level) { return DIRS_ALL | (uint32_t)level; }
inline uint32_t decreaseSkipOneDirection(int level, int d) { return (DIRS_ALL & ~dirBit(d)) | (uint32_t)level; }
inline uint32_t increaseFromEmission(int level) { return DIRS_ALL | FLAG_FROM_EMISSION | (uint32_t)level; }
inline uint32_t increaseSkipOneDirection(int level, int d) { return (DIRS_ALL & ~dirBit(d)) | (uint32_t)level; }
inline uint32_t increaseOnlyOneDirection(int level, int d) { return dirBit(d) | (uint32_t)level; }

// "向内拉光"条目：level=1 的全方向 decrease，借补光分支让所有亮邻居向本格回灌
constexpr uint32_t PULL_LIGHT_IN_ENTRY = DIRS_ALL | 1u;

struct QueueItem { uint64_t pos; uint32_t entry; };

} // namespace

Light::Light() {
    startWorkerThread();
}

Light::~Light() {
    workerRunning.store(false);
    inputCV.notify_all();
    if (workerThread.joinable()) {
        workerThread.join();
    }
}

void Light::startWorkerThread() {
    workerRunning.store(true);
    workerThread = std::thread(&Light::workerLoop, this);
}

void Light::workerLoop() {
    while (workerRunning.load()) {
        std::vector<uint64_t> nodes;
        {
            std::unique_lock<std::mutex> lock(inputMutex);
            inputCV.wait(lock, [this] { return !pendingNodes.empty() || !workerRunning.load(); });
            if (!workerRunning.load()) break;
            nodes.assign(pendingNodes.begin(), pendingNodes.end());
            pendingNodes.clear();
        }

        // 在工作线程上执行增量传播（不阻塞任何线程）
        if (chunkManagerRef) {
            runLightUpdates(chunkManagerRef, nodes);
        }
    }
}

void Light::queueLightRecalc(int worldX, int worldY, int worldZ) {
    std::lock_guard<std::mutex> lock(inputMutex);
    pendingNodes.insert(packPos(worldX, worldY, worldZ));
    inputCV.notify_one();
}

bool Light::pollDirtyLightChunks(std::vector<std::pair<int, int>>& outChunks) {
    std::lock_guard<std::mutex> lock(outputMutex);
    if (dirtyChunkKeys.empty()) return false;
    outChunks.clear();
    outChunks.reserve(dirtyChunkKeys.size());
    for (uint64_t key : dirtyChunkKeys) {
        outChunks.emplace_back((int)(int32_t)(key >> 32), (int)(int32_t)(key & 0xFFFFFFFFu));
    }
    dirtyChunkKeys.clear();
    return true;
}

void Light::runLightUpdates(ChunkManager* chunkMgr, const std::vector<uint64_t>& nodes) {
    if (!chunkMgr || nodes.empty()) return;

    auto* registry = ClientEngine::getInstance()->getBlockRegistry();

    // 区块缓存：避免反复 getChunk() 导致 mutex + map 查找开销
    std::unordered_map<uint64_t, std::shared_ptr<Chunk>> chunkCache;
    auto getCachedChunk = [&](int cx, int cz) -> Chunk* {
        uint64_t key = ((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz;
        auto it = chunkCache.find(key);
        if (it != chunkCache.end()) return it->second.get();
        auto chunk = chunkMgr->getChunk(cx, cz);
        chunkCache[key] = chunk;
        return chunk.get();
    };

    auto getSectionAt = [&](int worldX, int worldY, int worldZ) -> ChunkSection* {
        auto* chunk = getCachedChunk(worldX >> 4, worldZ >> 4);
        if (!chunk) return nullptr;
        auto& dim = chunk->dimension;
        if (worldY < dim.minY || worldY >= dim.maxY) return nullptr;
        int si = (worldY - dim.minY) / 16;
        if (si < 0 || si >= (int)chunk->sections.size()) return nullptr;
        return chunk->sections[si].get();
    };

    auto getStateAt = [&](int worldX, int worldY, int worldZ) -> int32_t {
        auto* chunk = getCachedChunk(worldX >> 4, worldZ >> 4);
        if (!chunk) return 0;
        return (int32_t)chunk->getBlockState(worldX & 15, worldY, worldZ & 15);
    };

    auto getBL = [&](int worldX, int worldY, int worldZ) -> uint8_t {
        auto* sec = getSectionAt(worldX, worldY, worldZ);
        if (!sec) return 0;
        return sec->getBlockLight(worldX & 15, worldY, worldZ & 15);
    };

    auto getSL = [&](int worldX, int worldY, int worldZ) -> uint8_t {
        auto* chunk = getCachedChunk(worldX >> 4, worldZ >> 4);
        if (!chunk) return 0;                  // 未加载 chunk 不作光源，避免向卸载区灌光
        auto& dim = chunk->dimension;
        if (worldY >= dim.maxY) return 15;     // 虚拟天空源：世界顶以上恒为满天空光
        if (worldY < dim.minY) return 0;
        int si = (worldY - dim.minY) / 16;
        if (si < 0 || si >= (int)chunk->sections.size()) return 0;
        auto* sec = chunk->sections[si].get();
        if (!sec) return 15;                   // 空 section 视为全亮（与快照/渲染端语义一致）
        return sec->getSkyLight(worldX & 15, worldY, worldZ & 15);
    };

    // 脏 chunk 收集：写入点所在 chunk + 跨界采样波及的相邻 chunk（平滑光照 remesh 需要）
    std::unordered_set<uint64_t> localDirty;
    auto markDirty = [&](int cx, int cz) {
        localDirty.insert(((uint64_t)(uint32_t)cx << 32) | (uint32_t)cz);
    };
    auto markDirtyAround = [&](int worldX, int worldZ) {
        int cx = worldX >> 4, cz = worldZ >> 4;
        markDirty(cx, cz);
        int lx = worldX & 15, lz = worldZ & 15;
        if (lx == 0) markDirty(cx - 1, cz);
        else if (lx == 15) markDirty(cx + 1, cz);
        if (lz == 0) markDirty(cx, cz - 1);
        else if (lz == 15) markDirty(cx, cz + 1);
    };
    auto setBL = [&](int worldX, int worldY, int worldZ, uint8_t val) {
        auto* sec = getSectionAt(worldX, worldY, worldZ);
        if (!sec) return;
        sec->setBlockLight(worldX & 15, worldY, worldZ & 15, val);
        markDirtyAround(worldX, worldZ);
    };
    auto setSL = [&](int worldX, int worldY, int worldZ, uint8_t val) {
        auto* sec = getSectionAt(worldX, worldY, worldZ);
        if (!sec) return;
        sec->setSkyLight(worldX & 15, worldY, worldZ & 15, val);
        markDirtyAround(worldX, worldZ);
    };

    // 方块属性查询：发光等级 / 是否挡光（均为预计算元数据，O(1)）
    auto getEmission = [&](int32_t state) -> int {
        if (state == 0) return 0;
        return registry->getBlockMetadata(state).emission;
    };
    auto blocksLight = [&](int32_t state) -> bool {
        if (state == 0) return false;
        const auto& meta = registry->getBlockMetadata(state);
        return meta.isFullBlock && meta.isOpaque;
    };
    // 半透光方块（水/树叶）：打断 15 级天空光的垂直无衰减下落
    auto isDiffuse = [&](int32_t state) -> bool {
        if (state == 0) return false;
        const auto& meta = registry->getBlockMetadata(state);
        return meta.isWater || meta.isLeaves;
    };

    // ===== 单层传播：方块光与天空光共用同一套双队列算法 =====
    // 天空光仅三处差异：
    // 1. 无方块发光源（emission 恒 0），光源是"虚拟天空"（getSL 在世界顶以上返回 15）
    // 2. 15 级天空光向正下方传播不衰减（skyFall），穿过水/树叶降为 14
    // 3. 变暗依赖判定需把"正下方同为 15"视为被本格灌注（skyFall 链）
    auto processLayer = [&](bool sky) {
        auto getL = [&](int x, int y, int z) -> int {
            return sky ? getSL(x, y, z) : getBL(x, y, z);
        };
        auto setL = [&](int x, int y, int z, uint8_t v) {
            if (sky) setSL(x, y, z, v); else setBL(x, y, z, v);
        };

        // 双 FIFO 队列（vector + 读指针，只 push_back 不删除）
        std::vector<QueueItem> decreaseQueue, increaseQueue;
        size_t decHead = 0, incHead = 0;
        auto enqueueDecrease = [&](uint64_t pos, uint32_t entry) { decreaseQueue.push_back({pos, entry}); };
        auto enqueueIncrease = [&](uint64_t pos, uint32_t entry) { increaseQueue.push_back({pos, entry}); };

        // ===== Phase 0: checkNode=====
        for (uint64_t node : nodes) {
            int x, y, z;
            unpackPos(node, x, y, z);
            int emission = sky ? 0 : getEmission(getStateAt(x, y, z));
            int stored = getL(x, y, z);
            if (emission < stored) {
                // 发光减弱/方块变挡光：清零自身，携带旧光值全方向变暗
                setL(x, y, z, 0);
                enqueueDecrease(node, decreaseAllDirections(stored));
            } else {
                // 向内拉光：让所有亮邻居稍后向本格单向补光
                enqueueDecrease(node, PULL_LIGHT_IN_ENTRY);
            }
            if (emission > 0) {
                enqueueIncrease(node, increaseFromEmission(emission));
            }
        }

        // ===== Phase 1: 排干变暗队列=====
        while (decHead < decreaseQueue.size()) {
            QueueItem item = decreaseQueue[decHead++];
            int x, y, z;
            unpackPos(item.pos, x, y, z);
            int fromLevel = entryLevel(item.entry);

            for (int d = 0; d < 6; d++) {
                if (!(item.entry & dirBit(d))) continue;
                int nx = x + DX[d], ny = y + DY[d], nz = z + DZ[d];
                int k = getL(nx, ny, nz);
                if (k == 0) continue;

                // skyFall 链：正下方同为 15 的天空光是被本格灌注的，同样视为依赖
                bool dependent = k <= fromLevel - 1
                              || (sky && d == DIR_DOWN && fromLevel == 15 && k == 15);
                if (dependent) {
                    // 邻居是被本格照亮的：清零并继续变暗；若邻居自身发光则重新入队变亮
                    int emission = sky ? 0 : getEmission(getStateAt(nx, ny, nz));
                    setL(nx, ny, nz, 0);
                    uint64_t npos = packPos(nx, ny, nz);
                    if (emission < k) {
                        enqueueDecrease(npos, decreaseSkipOneDirection(k, d ^ 1));
                    }
                    if (emission > 0) {
                        enqueueIncrease(npos, increaseFromEmission(emission));
                    }
                } else {
                    // 邻居有其他光源支撑：让它稍后朝本格方向单向补光
                    enqueueIncrease(packPos(nx, ny, nz), increaseOnlyOneDirection(k, d ^ 1));
                }
            }
        }

        // ===== Phase 2: 排干变亮队列=====
        while (incHead < increaseQueue.size()) {
            QueueItem item = increaseQueue[incHead++];
            int x, y, z;
            unpackPos(item.pos, x, y, z);
            int stored = getL(x, y, z);
            int fromLevel = entryLevel(item.entry);

            if ((item.entry & FLAG_FROM_EMISSION) && stored < fromLevel) {
                setL(x, y, z, (uint8_t)fromLevel);
                stored = fromLevel;
            }
            // 入队后光值变过则条目失效，跳过
            if (stored != fromLevel) continue;

            for (int d = 0; d < 6; d++) {
                if (!(item.entry & dirBit(d))) continue;
                int nx = x + DX[d], ny = y + DY[d], nz = z + DZ[d];
                int k = getL(nx, ny, nz);
                int newLevel = fromLevel - 1;
                // 15 级天空光垂直下落不衰减；可能把 k 抬回 15，不能提前拒绝
                bool skyFall = sky && d == DIR_DOWN && fromLevel == 15;
                if (!skyFall && newLevel <= k) continue;

                int32_t nstate = getStateAt(nx, ny, nz);
                if (blocksLight(nstate)) continue;
                if (skyFall && !isDiffuse(nstate)) newLevel = 15;
                if (newLevel <= k) continue;

                setL(nx, ny, nz, (uint8_t)newLevel);
                if (newLevel > 1) {
                    enqueueIncrease(packPos(nx, ny, nz), increaseSkipOneDirection(newLevel, d ^ 1));
                }
            }
        }
    };

    processLayer(false);  // 方块光
    processLayer(true);   // 天空光

    // 脏 chunk 合并到输出集合，渲染线程 poll 后精准 remesh
    if (!localDirty.empty()) {
        std::lock_guard<std::mutex> lock(outputMutex);
        dirtyChunkKeys.insert(localDirty.begin(), localDirty.end());
    }
}

