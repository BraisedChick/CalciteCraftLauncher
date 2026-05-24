#include "MeshGenerator.h"
#include "BlockRegistry.h"
#include "TextureAtlas.h"
#include "BiomeColorManager.h"
#include <android/log.h>
#include <map>
#include <string>
#include <cmath>

#define LOG_TAG "MeshGenerator"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 辅助函数：获取全局坐标的 blockState
int32_t MeshGenerator::getBlockStateAt(int x, int y, int z, const ChunkManager* chunkManager) {
    if (!chunkManager) return 0;  // 没有 ChunkManager，视为空气
    
    // 计算方块所在的区块坐标和本地坐标
    // 使用 floor division 处理负数
    int chunkX = (int)floor((float)x / 16.0f);
    int chunkZ = (int)floor((float)z / 16.0f);
    int localX = x - chunkX * 16;
    int localZ = z - chunkZ * 16;
    
    // 确保本地坐标在 [0, 15] 范围内
    if (localX < 0 || localX >= 16 || localZ < 0 || localZ >= 16) {
        LOGE("Invalid local coordinates: x=%d, z=%d, localX=%d, localZ=%d", x, z, localX, localZ);
        return 0;
    }
    
    // 获取区块（shared_ptr 保证线程安全）
    auto chunk = chunkManager->getChunk(chunkX, chunkZ);
    if (!chunk || !chunk->isLoaded) return 0;
    
    // 找到对应的 section
    for (const auto& section : chunk->sections) {
        if (!section || section->isEmpty) continue;
        
        int sectionMinY = section->y;
        int sectionMaxY = sectionMinY + SECTION_HEIGHT - 1;
        
        if (y >= sectionMinY && y <= sectionMaxY) {
            int localY = y - sectionMinY;
            int index = (localY * CHUNK_DEPTH + localZ) * CHUNK_WIDTH + localX;
            
            if (index >= 0 && index < static_cast<int>(section->blockStates.size())) {
                return section->blockStates[index];
            }
        }
    }
    
    return 0;  // 未找到，视为空气
}

std::vector<Vertex> MeshGenerator::generateMesh(const Chunk& chunk) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // 注意：这个函数没有 ChunkManager，无法进行跨区块剔除
    // 建议改用 generateMeshWithIndices 并传递 ChunkManager
    for (size_t sectionIdx = 0; sectionIdx < chunk.sections.size(); ++sectionIdx) {
        const auto& section = chunk.sections[sectionIdx];
        if (!section || section->isEmpty) continue;
        
        int sectionY = section->y;
        auto meshOut = generateSectionMesh(*section, chunk.pos.x, sectionY, chunk.pos.z, nullptr);

        // 调整索引偏移
        uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
        for (uint32_t idx : meshOut.indices) {
            indices.push_back(vertexOffset + idx);
        }

        vertices.insert(vertices.end(), meshOut.vertices.begin(), meshOut.vertices.end());
    }
    
    return vertices;
}

std::pair<std::vector<Vertex>, std::vector<uint32_t>> MeshGenerator::generateMeshWithIndices(const Chunk& chunk) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // 注意：这个函数没有 ChunkManager，无法进行跨区块剔除
    // 建议在 GLRenderer 中直接调用 generateSectionMesh 并传递 ChunkManager
    for (size_t sectionIdx = 0; sectionIdx < chunk.sections.size(); ++sectionIdx) {
        const auto& section = chunk.sections[sectionIdx];
        if (!section || section->isEmpty) continue;
        
        int sectionY = section->y;
        auto meshOut = generateSectionMesh(*section, chunk.pos.x, sectionY, chunk.pos.z, nullptr);

        // 调整索引偏移
        uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
        for (uint32_t idx : meshOut.indices) {
            indices.push_back(vertexOffset + idx);
        }

        vertices.insert(vertices.end(), meshOut.vertices.begin(), meshOut.vertices.end());
    }
    
    return {vertices, indices};
}

MeshGenerator::SectionMeshOutput MeshGenerator::generateSectionMesh(const ChunkSection& section,
                                                                    int chunkX, int sectionY, int chunkZ,
                                                                    const ChunkManager* chunkManager) {
    // ===== 三类几何体分开存储 =====
    std::vector<Vertex> baseVertices;      // 不透明方块（草、沙、石等）
    std::vector<uint32_t> baseIndices;

    std::vector<Vertex> overlayVertices;   // 草覆盖层（染色层）
    std::vector<uint32_t> overlayIndices;

    std::vector<Vertex> waterVertices;     // 水方块（半透明）
    std::vector<uint32_t> waterIndices;

    float baseX = chunkX * CHUNK_WIDTH;
    float baseY = static_cast<float>(sectionY);
    float baseZ = chunkZ * CHUNK_DEPTH;

    // ---- 相邻 Section blockStates 缓存 ----
    const int32_t* const selfData = section.blockStates.data();
    const size_t selfSize = section.blockStates.size();
    const int32_t* aboveData = nullptr;
    size_t aboveSize = 0;
    const int32_t* belowData = nullptr;
    size_t belowSize = 0;
    struct { const int32_t* data = nullptr; size_t size = 0; } horizCache[4];

    if (chunkManager) {
        auto thisChunk = chunkManager->getChunk(chunkX, chunkZ);
        if (thisChunk && thisChunk->isLoaded) {
            for (const auto& s : thisChunk->sections) {
                if (!s || s->isEmpty) continue;
                if (s->y == sectionY + SECTION_HEIGHT) {
                    aboveData = s->blockStates.data();
                    aboveSize = s->blockStates.size();
                } else if (s->y == sectionY - SECTION_HEIGHT) {
                    belowData = s->blockStates.data();
                    belowSize = s->blockStates.size();
                }
            }
        }

        auto preloadHoriz = [&](int idx, int cx, int cz) {
            auto chunk = chunkManager->getChunk(cx, cz);
            if (!chunk || !chunk->isLoaded) return;
            for (const auto& s : chunk->sections) {
                if (!s || s->isEmpty) continue;
                if (s->y == sectionY) {
                    horizCache[idx].data = s->blockStates.data();
                    horizCache[idx].size = s->blockStates.size();
                    break;
                }
            }
        };
        preloadHoriz(0, chunkX + 1, chunkZ);
        preloadHoriz(1, chunkX - 1, chunkZ);
        preloadHoriz(2, chunkX, chunkZ + 1);
        preloadHoriz(3, chunkX, chunkZ - 1);
    }

    // 获取局部坐标的 blockState
    auto getLocalBlockState = [&](int x, int y, int z) -> int32_t {
        if (x >= 0 && x < CHUNK_WIDTH && y >= 0 && y < SECTION_HEIGHT && z >= 0 && z < CHUNK_DEPTH) {
            int idx = (y * CHUNK_DEPTH + z) * CHUNK_WIDTH + x;
            if (idx < static_cast<int>(selfSize)) return selfData[idx];
            return 0;
        }
        if (x >= 0 && x < CHUNK_WIDTH && z >= 0 && z < CHUNK_DEPTH) {
            if (y < 0 && belowData) {
                int localY = y + SECTION_HEIGHT;
                if (localY >= 0 && localY < SECTION_HEIGHT) {
                    int idx = (localY * CHUNK_DEPTH + z) * CHUNK_WIDTH + x;
                    if (idx < static_cast<int>(belowSize)) return belowData[idx];
                }
                return 0;
            }
            if (y >= SECTION_HEIGHT && aboveData) {
                int localY = y - SECTION_HEIGHT;
                if (localY >= 0 && localY < SECTION_HEIGHT) {
                    int idx = (localY * CHUNK_DEPTH + z) * CHUNK_WIDTH + x;
                    if (idx < static_cast<int>(aboveSize)) return aboveData[idx];
                }
                return 0;
            }
            return 0;
        }
        int cacheIdx = -1;
        int localX = x, localZ = z;
        if (x < 0)                 { cacheIdx = 1; localX = x + CHUNK_WIDTH; }
        else if (x >= CHUNK_WIDTH) { cacheIdx = 0; localX = x - CHUNK_WIDTH; }
        if (z < 0)                 { cacheIdx = 3; localZ = z + CHUNK_DEPTH; }
        else if (z >= CHUNK_DEPTH) { cacheIdx = 2; localZ = z - CHUNK_DEPTH; }
        if (cacheIdx >= 0 && horizCache[cacheIdx].data &&
            localX >= 0 && localX < CHUNK_WIDTH &&
            y >= 0 && y < SECTION_HEIGHT &&
            localZ >= 0 && localZ < CHUNK_DEPTH) {
            int idx = (y * CHUNK_DEPTH + localZ) * CHUNK_WIDTH + localX;
            if (idx < static_cast<int>(horizCache[cacheIdx].size)) {
                return horizCache[cacheIdx].data[idx];
            }
        }
        if (chunkManager) {
            int globalX = chunkX * CHUNK_WIDTH + x;
            int globalY = sectionY + y;
            int globalZ = chunkZ * CHUNK_DEPTH + z;
            return getBlockStateAt(globalX, globalY, globalZ, chunkManager);
        }
        return 0;
    };

    // 辅助函数：判断是否为完整方块
    auto isSolid = [&](int32_t state) -> bool {
        if (state == 0) return false;
        auto& meta = BlockRegistry::getInstance().getBlockMetadata(state);
        return meta.isFullBlock;
    };

    // 遍历所有方块
    for (int localY = 0; localY < SECTION_HEIGHT; localY++) {
        for (int z = 0; z < CHUNK_DEPTH; z++) {
            for (int x = 0; x < CHUNK_WIDTH; x++) {
                int index = (localY * CHUNK_DEPTH + z) * CHUNK_WIDTH + x;
                if (index >= static_cast<int>(section.blockStates.size())) continue;

                int32_t blockState = section.blockStates[index];
                if (blockState == 0) continue;

                auto& registry = BlockRegistry::getInstance();
                const auto& blockMeta = registry.getBlockMetadata(blockState);

                // 获取生物群系颜色
                uint8_t tintR = 255, tintG = 255, tintB = 255;
                int biomeIdx = ((localY >> 2) << 4) | ((z >> 2) << 2) | (x >> 2);
                int32_t biomeId = 0;
                if (biomeIdx < static_cast<int>(section.biomes.size())) {
                    biomeId = section.biomes[biomeIdx];
                }
                if (blockMeta.isGrassBlock) {
                    BiomeColorManager::getInstance().getGrassColor(biomeId, tintR, tintG, tintB);
                } else if (blockMeta.isLeaves) {
                    BiomeColorManager::getInstance().getFoliageColor(biomeId, tintR, tintG, tintB);
                }

                BlockTextureConfig tex{blockMeta.texTop, blockMeta.texSide, blockMeta.texBottom};
                float blockHeight = blockMeta.height;
                bool isFullBlockHeight = blockMeta.height >= 1.0f;

                float posX = baseX + x;
                float posY = baseY + localY;
                float posZ = baseZ + z;

                // 获取6个邻居
                int32_t n[6];
                n[0] = getLocalBlockState(x, localY + 1, z);
                n[1] = getLocalBlockState(x, localY - 1, z);
                n[2] = getLocalBlockState(x + 1, localY, z);
                n[3] = getLocalBlockState(x - 1, localY, z);
                n[4] = getLocalBlockState(x, localY, z + 1);
                n[5] = getLocalBlockState(x, localY, z - 1);

                // ===== 植物 =====
                // ===== 植物类方块 =====
                if (blockMeta.isPlant) {
                    // 获取植物颜色（使用草的颜色算法）
                    uint8_t plantR = 255, plantG = 255, plantB = 255;
                    BiomeColorManager::getInstance().getGrassColor(biomeId, plantR, plantG, plantB);

                    float plantTexIndex = static_cast<float>(tex.top);
                    uint32_t baseIdx = static_cast<uint32_t>(baseVertices.size());

                    // 四边形1
                    baseVertices.push_back({{posX, posY, posZ + 0.5f}, {0.0f, 1.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});
                    baseVertices.push_back({{posX + 1.0f, posY, posZ + 0.5f}, {1.0f, 1.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});
                    baseVertices.push_back({{posX + 1.0f, posY + 1.0f, posZ + 0.5f}, {1.0f, 0.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});
                    baseVertices.push_back({{posX, posY + 1.0f, posZ + 0.5f}, {0.0f, 0.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});

                    baseIndices.push_back(baseIdx); baseIndices.push_back(baseIdx + 1); baseIndices.push_back(baseIdx + 2);
                    baseIndices.push_back(baseIdx); baseIndices.push_back(baseIdx + 2); baseIndices.push_back(baseIdx + 3);
                    baseIndices.push_back(baseIdx + 2); baseIndices.push_back(baseIdx + 1); baseIndices.push_back(baseIdx + 0);
                    baseIndices.push_back(baseIdx + 3); baseIndices.push_back(baseIdx + 2); baseIndices.push_back(baseIdx + 0);

                    baseIdx += 4;

                    // 四边形2
                    baseVertices.push_back({{posX + 0.5f, posY, posZ}, {0.0f, 1.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});
                    baseVertices.push_back({{posX + 0.5f, posY, posZ + 1.0f}, {1.0f, 1.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});
                    baseVertices.push_back({{posX + 0.5f, posY + 1.0f, posZ + 1.0f}, {1.0f, 0.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});
                    baseVertices.push_back({{posX + 0.5f, posY + 1.0f, posZ}, {0.0f, 0.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});

                    baseIndices.push_back(baseIdx); baseIndices.push_back(baseIdx + 1); baseIndices.push_back(baseIdx + 2);
                    baseIndices.push_back(baseIdx); baseIndices.push_back(baseIdx + 2); baseIndices.push_back(baseIdx + 3);
                    baseIndices.push_back(baseIdx + 2); baseIndices.push_back(baseIdx + 1); baseIndices.push_back(baseIdx + 0);
                    baseIndices.push_back(baseIdx + 3); baseIndices.push_back(baseIdx + 2); baseIndices.push_back(baseIdx + 0);

                    continue;
                }

                // ===== 水 =====
                if (blockMeta.isWater) {
                    float waterTexIndex = static_cast<float>(tex.top);
                    uint8_t waterR = 255, waterG = 255, waterB = 255;
                    BiomeColorManager::getInstance().getWaterColor(biomeId, waterR, waterG, waterB);

                    auto isWaterBlock = [&](int32_t state) -> bool {
                        return state != 0 && BlockRegistry::getInstance().getBlockMetadata(state).isWater;
                    };

                    auto addWaterFace = [&](float x1, float y1, float z1, float x2, float y2, float z2,
                                            float x3, float y3, float z3, float x4, float y4, float z4) {
                        uint32_t ob = static_cast<uint32_t>(waterVertices.size());
                        waterVertices.push_back({{x1, y1, z1}, {0.0f, 1.0f}, waterTexIndex, {waterR, waterG, waterB, 180}});
                        waterVertices.push_back({{x2, y2, z2}, {1.0f, 1.0f}, waterTexIndex, {waterR, waterG, waterB, 180}});
                        waterVertices.push_back({{x3, y3, z3}, {1.0f, 0.0f}, waterTexIndex, {waterR, waterG, waterB, 180}});
                        waterVertices.push_back({{x4, y4, z4}, {0.0f, 0.0f}, waterTexIndex, {waterR, waterG, waterB, 180}});
                        waterIndices.push_back(ob); waterIndices.push_back(ob + 1); waterIndices.push_back(ob + 2);
                        waterIndices.push_back(ob); waterIndices.push_back(ob + 2); waterIndices.push_back(ob + 3);
                    };

                    // 顶面
                    if (!isWaterBlock(n[0])) {
                        addWaterFace(posX, posY + 1.0f, posZ + 1.0f,
                                     posX + 1.0f, posY + 1.0f, posZ + 1.0f,
                                     posX + 1.0f, posY + 1.0f, posZ,
                                     posX, posY + 1.0f, posZ);
                    }
                    // 前面
                    if (!isWaterBlock(n[4]) && (n[4] == 0 || !isSolid(n[4]))) {
                        addWaterFace(posX, posY, posZ + 1.0f,
                                     posX + 1.0f, posY, posZ + 1.0f,
                                     posX + 1.0f, posY + 1.0f, posZ + 1.0f,
                                     posX, posY + 1.0f, posZ + 1.0f);
                    }
                    // 后面
                    if (!isWaterBlock(n[5]) && (n[5] == 0 || !isSolid(n[5]))) {
                        addWaterFace(posX + 1.0f, posY, posZ,
                                     posX, posY, posZ,
                                     posX, posY + 1.0f, posZ,
                                     posX + 1.0f, posY + 1.0f, posZ);
                    }
                    // 右面
                    if (!isWaterBlock(n[2]) && (n[2] == 0 || !isSolid(n[2]))) {
                        addWaterFace(posX + 1.0f, posY, posZ + 1.0f,
                                     posX + 1.0f, posY, posZ,
                                     posX + 1.0f, posY + 1.0f, posZ,
                                     posX + 1.0f, posY + 1.0f, posZ + 1.0f);
                    }
                    // 左面
                    if (!isWaterBlock(n[3]) && (n[3] == 0 || !isSolid(n[3]))) {
                        addWaterFace(posX, posY, posZ,
                                     posX, posY, posZ + 1.0f,
                                     posX, posY + 1.0f, posZ + 1.0f,
                                     posX, posY + 1.0f, posZ);
                    }
                    continue;
                }

                // ===== 普通不透明方块 =====
                bool renderTop = (n[0] == 0) || !isSolid(n[0]);
                bool isSnowCovered = (blockMeta.isGrassBlock && n[0] != 0 &&
                                      BlockRegistry::getInstance().getBlockMetadata(n[0]).isSnow);
                int sideTexIndex = isSnowCovered ? TEX_GRASS_BLOCK_SNOW : tex.side;

                // 顶面
                if (renderTop) {
                    float texIndex = static_cast<float>(tex.top);
                    uint32_t faceBase = static_cast<uint32_t>(baseVertices.size());
                    baseVertices.push_back({{posX, posY + blockHeight, posZ + 1.0f}, {0.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    baseVertices.push_back({{posX + 1.0f, posY + blockHeight, posZ + 1.0f}, {1.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    baseVertices.push_back({{posX + 1.0f, posY + blockHeight, posZ}, {1.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    baseVertices.push_back({{posX, posY + blockHeight, posZ}, {0.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    baseIndices.push_back(faceBase); baseIndices.push_back(faceBase + 1); baseIndices.push_back(faceBase + 2);
                    baseIndices.push_back(faceBase); baseIndices.push_back(faceBase + 2); baseIndices.push_back(faceBase + 3);
                }

                // 底面
                if (isFullBlockHeight && (n[1] == 0 || !isSolid(n[1]))) {
                    float texIndex = static_cast<float>(tex.bottom);
                    uint32_t faceBase = static_cast<uint32_t>(baseVertices.size());
                    baseVertices.push_back({{posX, posY, posZ}, {0.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    baseVertices.push_back({{posX + 1.0f, posY, posZ}, {1.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    baseVertices.push_back({{posX + 1.0f, posY, posZ + 1.0f}, {1.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    baseVertices.push_back({{posX, posY, posZ + 1.0f}, {0.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    baseIndices.push_back(faceBase); baseIndices.push_back(faceBase + 1); baseIndices.push_back(faceBase + 2);
                    baseIndices.push_back(faceBase); baseIndices.push_back(faceBase + 2); baseIndices.push_back(faceBase + 3);
                }

                // 草覆盖层标志
                bool needsGrassOverlay = false;
                float baseSideTexIndex;
                uint8_t baseSideColor[4];

                if (blockMeta.isGrassBlock) {
                    if (isSnowCovered) {
                        baseSideTexIndex = static_cast<float>(TEX_GRASS_BLOCK_SNOW);
                        baseSideColor[0] = 255; baseSideColor[1] = 255; baseSideColor[2] = 255; baseSideColor[3] = 255;
                    } else {
                        baseSideTexIndex = static_cast<float>(TEX_GRASS_SIDE);
                        baseSideColor[0] = 255; baseSideColor[1] = 255; baseSideColor[2] = 255; baseSideColor[3] = 255;
                        needsGrassOverlay = true;
                    }
                } else {
                    baseSideTexIndex = static_cast<float>(sideTexIndex);
                    baseSideColor[0] = tintR; baseSideColor[1] = tintG; baseSideColor[2] = tintB; baseSideColor[3] = 255;
                }

                // 侧面生成
                auto addSide = [&](int neighborIdx, bool isNeighborSolid,
                                   float x1, float y1, float z1,
                                   float x2, float y2, float z2,
                                   float x3, float y3, float z3,
                                   float x4, float y4, float z4) {
                    if (n[neighborIdx] != 0 && isNeighborSolid) return;

                    uint32_t faceBase = static_cast<uint32_t>(baseVertices.size());
                    baseVertices.push_back({{x1, y1, z1}, {0.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    baseVertices.push_back({{x2, y2, z2}, {1.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    baseVertices.push_back({{x3, y3, z3}, {1.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    baseVertices.push_back({{x4, y4, z4}, {0.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    baseIndices.push_back(faceBase); baseIndices.push_back(faceBase + 1); baseIndices.push_back(faceBase + 2);
                    baseIndices.push_back(faceBase); baseIndices.push_back(faceBase + 2); baseIndices.push_back(faceBase + 3);

                    if (needsGrassOverlay) {
                        float overlayTex = static_cast<float>(TEX_GRASS_SIDE_OVERLAY);
                        uint32_t ob = static_cast<uint32_t>(overlayVertices.size());
                        overlayVertices.push_back({{x1, y1, z1}, {0.0f, 1.0f}, overlayTex, {tintR, tintG, tintB, 255}});
                        overlayVertices.push_back({{x2, y2, z2}, {1.0f, 1.0f}, overlayTex, {tintR, tintG, tintB, 255}});
                        overlayVertices.push_back({{x3, y3, z3}, {1.0f, 0.0f}, overlayTex, {tintR, tintG, tintB, 255}});
                        overlayVertices.push_back({{x4, y4, z4}, {0.0f, 0.0f}, overlayTex, {tintR, tintG, tintB, 255}});
                        overlayIndices.push_back(ob); overlayIndices.push_back(ob + 1); overlayIndices.push_back(ob + 2);
                        overlayIndices.push_back(ob); overlayIndices.push_back(ob + 2); overlayIndices.push_back(ob + 3);
                    }
                };

                bool neighborSolid[6];
                for (int i = 0; i < 6; i++) {
                    neighborSolid[i] = (n[i] != 0) && isSolid(n[i]);
                }

                // 前面 (z+)
                addSide(4, neighborSolid[4],
                        posX, posY, posZ + 1.0f,
                        posX + 1.0f, posY, posZ + 1.0f,
                        posX + 1.0f, posY + blockHeight, posZ + 1.0f,
                        posX, posY + blockHeight, posZ + 1.0f);
                // 后面 (z-)
                addSide(5, neighborSolid[5],
                        posX + 1.0f, posY, posZ,
                        posX, posY, posZ,
                        posX, posY + blockHeight, posZ,
                        posX + 1.0f, posY + blockHeight, posZ);
                // 右面 (x+)
                addSide(2, neighborSolid[2],
                        posX + 1.0f, posY, posZ + 1.0f,
                        posX + 1.0f, posY, posZ,
                        posX + 1.0f, posY + blockHeight, posZ,
                        posX + 1.0f, posY + blockHeight, posZ + 1.0f);
                // 左面 (x-)
                addSide(3, neighborSolid[3],
                        posX, posY, posZ,
                        posX, posY, posZ + 1.0f,
                        posX, posY + blockHeight, posZ + 1.0f,
                        posX, posY + blockHeight, posZ);
            }
        }
    }

    // ===== 按顺序合并：base → grass_overlay → water =====
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;

    // 1. 基础不透明方块
    uint32_t baseVertexCount = static_cast<uint32_t>(baseVertices.size());
    allVertices.insert(allVertices.end(), baseVertices.begin(), baseVertices.end());
    allIndices.insert(allIndices.end(), baseIndices.begin(), baseIndices.end());

    // 2. 草覆盖层（需要调整索引偏移）
    if (!overlayVertices.empty()) {
        uint32_t overlayStart = static_cast<uint32_t>(allVertices.size());
        for (uint32_t idx : overlayIndices) {
            allIndices.push_back(overlayStart + idx);
        }
        allVertices.insert(allVertices.end(), overlayVertices.begin(), overlayVertices.end());
    }
    uint32_t overlayIndexCount = static_cast<uint32_t>(overlayIndices.size());

    // 3. 水（需要调整索引偏移）
    uint32_t waterIndexCount = 0;
    if (!waterVertices.empty()) {
        uint32_t waterStart = static_cast<uint32_t>(allVertices.size());
        for (uint32_t idx : waterIndices) {
            allIndices.push_back(waterStart + idx);
        }
        allVertices.insert(allVertices.end(), waterVertices.begin(), waterVertices.end());
        waterIndexCount = static_cast<uint32_t>(waterIndices.size());
    }

    return {std::move(allVertices), std::move(allIndices), overlayIndexCount, waterIndexCount};
}

void MeshGenerator::addFace(std::vector<Vertex>& vertices, float x, float y, float z,
                            float nx, float ny, float nz,
                            float r, float g, float b) {
    Vertex vertex;
    vertex.pos[0] = x;
    vertex.pos[1] = y;
    vertex.pos[2] = z;
    vertices.push_back(vertex);
}

uint32_t MeshGenerator::getBlockColor(uint16_t blockId) {
    // 简化的方块颜色映射
    switch (blockId) {
        case 1: return 0x8B4513; // 泥土
        case 2: return 0x228B22; // 草方块
        case 3: return 0x808080; // 石头
        default: return 0xFFFFFF;
    }
}