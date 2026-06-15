#include "Light.h"
#include "ChunkManager.h"
#include "BlockRegistry.h"
#include <android/log.h>
#include <cmath>
#include <algorithm>
#include <queue>
#include <cstring>
#include <string>

#define LOG_TAG "Light"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

Light& Light::getInstance() {
    static Light instance;
    return instance;
}

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

// ===== 光照贴图创建 =====

void Light::createLightmapTexture() {
    glGenTextures(1, &lightmapTextureID);
    glBindTexture(GL_TEXTURE_2D, lightmapTextureID);

    // 初始生成（全白天亮度，首帧 update() 会覆盖）
    uint8_t pixels[16 * 16 * 4];
    generateLightmapPixels(1.0f, pixels);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    LOGI("Created lightmap texture 16x16 with colored lighting (warm block/cool sky)");
}

// ===== 每帧更新 =====

void Light::update() {
    float skyDarken = getSkyDarken();
    float skyBright = 1.0f - skyDarken;

    // 天空/雾效颜色插值：白天蓝 → 夜晚深蓝黑
    skyR = 0.53f * skyBright + 0.02f * skyDarken;
    skyG = 0.81f * skyBright + 0.02f * skyDarken;
    skyB = 0.92f * skyBright + 0.08f * skyDarken;

    // 更新光照贴图纹理
    if (lightmapTextureID != 0) {
        uint8_t pixels[16 * 16 * 4];
        generateLightmapPixels(skyBright, pixels);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, lightmapTextureID);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }
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

// ===== 客户端方块光 BFS 传播 =====

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
        int wx, wy, wz;
        {
            std::unique_lock<std::mutex> lock(inputMutex);
            inputCV.wait(lock, [this] { return hasInput || !workerRunning.load(); });
            if (!workerRunning.load()) break;
            wx = pendingLightX;
            wy = pendingLightY;
            wz = pendingLightZ;
            hasInput = false;
        }

        // 在工作线程上执行 BFS（不阻塞任何线程）
        if (chunkManagerRef) {
            recalcBlockLight(chunkManagerRef, wx, wy, wz);

            // 结果入队，渲染线程 poll
            std::lock_guard<std::mutex> lock(outputMutex);
            completedX = wx;
            completedY = wy;
            completedZ = wz;
            hasOutput = true;
        }
    }
}

void Light::queueBlockLightRecalc(int worldX, int worldY, int worldZ) {
    std::lock_guard<std::mutex> lock(inputMutex);
    pendingLightX = worldX;
    pendingLightY = worldY;
    pendingLightZ = worldZ;
    hasInput = true;
    inputCV.notify_one();
}

bool Light::pollCompletedLightRecalc(int* outX, int* outY, int* outZ) {
    std::lock_guard<std::mutex> lock(outputMutex);
    if (!hasOutput) return false;
    if (outX) *outX = completedX;
    if (outY) *outY = completedY;
    if (outZ) *outZ = completedZ;
    hasOutput = false;
    return true;
}

void Light::recalcBlockLight(ChunkManager* chunkMgr, int wx, int wy, int wz) {
    if (!chunkMgr) return;

    auto& registry = BlockRegistry::getInstance();

    // 区块缓存：避免反复 getChunk() 导致 mutex + map 查找开销
    std::unordered_map<uint64_t, std::shared_ptr<Chunk>> chunkCache;
    auto getCachedChunk = [&](int cx, int cz) -> std::shared_ptr<Chunk> {
        uint64_t key = ((uint64_t)(cx & 0xFFFFFFFF) << 32) | (cz & 0xFFFFFFFF);
        auto it = chunkCache.find(key);
        if (it != chunkCache.end()) return it->second;
        auto chunk = chunkMgr->getChunk(cx, cz);
        chunkCache[key] = chunk;
        return chunk;
    };

    // 辅助：获取指定世界坐标的 ChunkSection
    auto getSectionAt = [&](int worldX, int worldY, int worldZ) -> ChunkSection* {
        int cx = worldX >> 4;
        int cz = worldZ >> 4;
        auto chunk = getCachedChunk(cx, cz);
        if (!chunk) return nullptr;
        auto& dim = chunk->dimension;
        if (worldY < dim.minY || worldY >= dim.maxY) return nullptr;
        int si = (worldY - dim.minY) / 16;
        if (si < 0 || si >= (int)chunk->sections.size()) return nullptr;
        return chunk->sections[si].get();
    };

    // 辅助：获取世界坐标的 blockState
    auto getStateAt = [&](int worldX, int worldY, int worldZ) -> int32_t {
        int cx = worldX >> 4;
        int cz = worldZ >> 4;
        auto chunk = getCachedChunk(cx, cz);
        if (!chunk) return 0;
        return chunk->getBlockState(worldX & 15, worldY, worldZ & 15);
    };

    // 辅助：获取/设置世界坐标的方块光
    auto getBL = [&](int worldX, int worldY, int worldZ) -> uint8_t {
        auto* sec = getSectionAt(worldX, worldY, worldZ);
        if (!sec) return 0;
        return sec->getBlockLight(worldX & 15, worldY, worldZ & 15);
    };
    auto setBL = [&](int worldX, int worldY, int worldZ, uint8_t val) {
        auto* sec = getSectionAt(worldX, worldY, worldZ);
        if (sec) sec->setBlockLight(worldX & 15, worldY, worldZ & 15, val);
    };

    const int CLEAR_R = 16;   // 清除半径（曼哈顿距离）
    const int SCAN_R = 31;    // 扫描半径 = CLEAR_R + 15（最远光源影响范围）

    // Step 1: 清除受影响区域的方块光
    for (int dy = -CLEAR_R; dy <= CLEAR_R; dy++) {
        for (int dz = -CLEAR_R; dz <= CLEAR_R; dz++) {
            for (int dx = -CLEAR_R; dx <= CLEAR_R; dx++) {
                if (abs(dx) + abs(dy) + abs(dz) > CLEAR_R) continue;
                setBL(wx + dx, wy + dy, wz + dz, 0);
            }
        }
    }

    // Step 2: 扫描更大范围，找到所有发光源
    struct LightNode { int x, y, z; uint8_t light; };
    std::queue<LightNode> bfsQueue;

    for (int dy = -SCAN_R; dy <= SCAN_R; dy++) {
        for (int dz = -SCAN_R; dz <= SCAN_R; dz++) {
            for (int dx = -SCAN_R; dx <= SCAN_R; dx++) {
                if (abs(dx) + abs(dy) + abs(dz) > SCAN_R) continue;
                int bx = wx + dx, by = wy + dy, bz = wz + dz;
                int32_t state = getStateAt(bx, by, bz);
                if (state == 0) continue;
                const auto* info = registry.getBlockInfo(state);
                if (!info) continue;
                int emission = getBlockEmission(info->name.c_str());
                if (emission > 0) {
                    setBL(bx, by, bz, (uint8_t)emission);
                    bfsQueue.push({bx, by, bz, (uint8_t)emission});
                }
            }
        }
    }

    // Step 3: BFS 光传播
    static const int DX[] = {1, -1, 0, 0, 0, 0};
    static const int DY[] = {0, 0, 1, -1, 0, 0};
    static const int DZ[] = {0, 0, 0, 0, 1, -1};

    while (!bfsQueue.empty()) {
        auto [nx, ny, nz, nl] = bfsQueue.front();
        bfsQueue.pop();

        for (int d = 0; d < 6; d++) {
            int ax = nx + DX[d], ay = ny + DY[d], az = nz + DZ[d];
            if (abs(ax - wx) + abs(ay - wy) + abs(az - wz) > SCAN_R) continue;

            uint8_t newLight = (nl > 0) ? (uint8_t)(nl - 1) : 0;
            if (newLight == 0) continue;

            int32_t neighborState = getStateAt(ax, ay, az);
            if (neighborState != 0) {
                const auto& meta = registry.getBlockMetadata(neighborState);
                if (meta.isFullBlock && meta.isOpaque) continue;
            }

            uint8_t current = getBL(ax, ay, az);
            if (newLight > current) {
                setBL(ax, ay, az, newLight);
                bfsQueue.push({ax, ay, az, newLight});
            }
        }
    }
}
