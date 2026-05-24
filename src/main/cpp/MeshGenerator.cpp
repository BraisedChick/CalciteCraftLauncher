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
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t overlayIndexCount = 0;
    // overlay 顶点/索引暂存区，最后合并到主缓冲区末尾（确保 base 在前 overlay 在后）
    std::vector<Vertex> overlayVertices;
    std::vector<uint32_t> overlayIndices;

    float baseX = chunkX * CHUNK_WIDTH;
    float baseY = static_cast<float>(sectionY);  // sectionY 已经是绝对坐标
    float baseZ = chunkZ * CHUNK_DEPTH;
    
    // ---- 相邻 Section blockStates 缓存（优化不可见面剔除）----
    // 预先加载 6 个相邻方向的 blockStates，避免逐方块重复跨区块查询
    const int32_t* const selfData = section.blockStates.data();
    const size_t selfSize = section.blockStates.size();
    const int32_t* aboveData = nullptr;
    size_t aboveSize = 0;
    const int32_t* belowData = nullptr;
    size_t belowSize = 0;
    // 4 个水平方向：0=+X, 1=-X, 2=+Z, 3=-Z
    struct { const int32_t* data = nullptr; size_t size = 0; } horizCache[4];

    if (chunkManager) {
        // 垂直方向：同 chunk 的上下 section
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

        // 水平方向：相邻 chunk 的同一层 section
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

    // 获取局部坐标的 blockState（使用缓存，避免跨区块查询开销）
    auto getLocalBlockState = [&](int x, int y, int z) -> int32_t {
        // 在当前 Section 范围内 → 直接数组访问
        if (x >= 0 && x < CHUNK_WIDTH &&
            y >= 0 && y < SECTION_HEIGHT &&
            z >= 0 && z < CHUNK_DEPTH) {
            int idx = (y * CHUNK_DEPTH + z) * CHUNK_WIDTH + x;
            if (idx < static_cast<int>(selfSize)) return selfData[idx];
            return 0;
        }

        // Y 轴越界 → 查询同 chunk 的上下 section（已缓存）
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

        // X 或 Z 越界 → 查询水平相邻 chunk 的缓存
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

        // 回退（极少触发：相邻 chunk 未加载或对角越界）
        if (chunkManager) {
            int globalX = chunkX * CHUNK_WIDTH + x;
            int globalY = sectionY + y;
            int globalZ = chunkZ * CHUNK_DEPTH + z;
            return getBlockStateAt(globalX, globalY, globalZ, chunkManager);
        }
        return 0;
    };


    // 遍历截面中的所有方块 (16x16x16)
    for (int localY = 0; localY < SECTION_HEIGHT; localY++) {
        for (int z = 0; z < CHUNK_DEPTH; z++) {
            for (int x = 0; x < CHUNK_WIDTH; x++) {
                // 计算在 blockStates 数组中的索引
                int index = (localY * CHUNK_DEPTH + z) * CHUNK_WIDTH + x;
                
                if (index >= static_cast<int>(section.blockStates.size())) {
                    continue;
                }
                
                int32_t blockState = section.blockStates[index];
                
                
                // 跳过空气方块 (block state 为 0)
                if (blockState == 0) {
                    continue;
                }
                

                // ===== 生物群系染色与方块元数据（一次性预计算，避免重复字符串解析）=====
                uint8_t tintR = 255, tintG = 255, tintB = 255;
                auto& registry = BlockRegistry::getInstance();
                const auto& blockMeta = registry.getBlockMetadata(blockState);
                {
                    int biomeIdx = ((localY >> 2) << 4) | ((z >> 2) << 2) | (x >> 2);
                    int32_t biomeId = 0;
                    if (biomeIdx < static_cast<int>(section.biomes.size())) {
                        biomeId = section.biomes[biomeIdx];
                    }
                    if (blockMeta.isGrassBlock) {
                        BiomeColorManager::getInstance().getGrassColor(biomeId, tintR, tintG, tintB);
                    } else if (blockMeta.isLeaves) {
                        BiomeColorManager::getInstance().getFoliageColor(biomeId, tintR, tintG, tintB);
                    } else if (blockMeta.name == "grass" || blockMeta.name == "tall_grass" || blockMeta.name == "fern" || blockMeta.name == "large_fern") {
                        BiomeColorManager::getInstance().getGrassColor(biomeId, tintR, tintG, tintB);
                    }
                }

                // 查询方块纹理配置（各面的纹理层索引）
                BlockTextureConfig tex{blockMeta.texTop, blockMeta.texSide, blockMeta.texBottom};

                // 查询方块高度（完整方块=1.0，雪片<1.0）
                float blockHeight = blockMeta.height;
                bool isFullBlockHeight = blockMeta.height >= 1.0f;

                // 计算方块的绝对坐标
                float posX = baseX + x;
                float posY = baseY + localY;
                float posZ = baseZ + z;

                // ===== 植物类方块：十字交叉渲染（两个垂直四边形交叉成 X 形）=====
                if (blockMeta.isPlant) {
                    float plantTexIndex = static_cast<float>(tex.top);
                    uint32_t baseIdx = static_cast<uint32_t>(vertices.size());

                    // 四边形1：沿 X 轴方向（在 z+0.5 位置）
                    vertices.push_back({{posX, posY, posZ + 0.5f}, {0.0f, 1.0f}, plantTexIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 1.0f, posY, posZ + 0.5f}, {1.0f, 1.0f}, plantTexIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 1.0f, posY + 1.0f, posZ + 0.5f}, {1.0f, 0.0f}, plantTexIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX, posY + 1.0f, posZ + 0.5f}, {0.0f, 0.0f}, plantTexIndex, {tintR, tintG, tintB, 255}});

                    // 正面 (CCW)
                    indices.push_back(baseIdx + 0);
                    indices.push_back(baseIdx + 1);
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 0);
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 3);

                    // 背面 (CW) — 双面可见，不依赖 GL_CULL_FACE 关闭
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 1);
                    indices.push_back(baseIdx + 0);
                    indices.push_back(baseIdx + 3);
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 0);

                    baseIdx += 4;

                    // 四边形2：沿 Z 轴方向（在 x+0.5 位置）
                    vertices.push_back({{posX + 0.5f, posY, posZ}, {0.0f, 1.0f}, plantTexIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 0.5f, posY, posZ + 1.0f}, {1.0f, 1.0f}, plantTexIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 0.5f, posY + 1.0f, posZ + 1.0f}, {1.0f, 0.0f}, plantTexIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 0.5f, posY + 1.0f, posZ}, {0.0f, 0.0f}, plantTexIndex, {tintR, tintG, tintB, 255}});

                    // 正面 (CCW)
                    indices.push_back(baseIdx + 0);
                    indices.push_back(baseIdx + 1);
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 0);
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 3);

                    // 背面 (CW)
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 1);
                    indices.push_back(baseIdx + 0);
                    indices.push_back(baseIdx + 3);
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 0);

                    continue; // 跳过下方的立方体面渲染逻辑
                }

                // 一次性获取 6 个邻居的 blockState，避免逐面重复调用
                int32_t n[6];
                n[0] = getLocalBlockState(x, localY + 1, z);  // 上
                n[1] = getLocalBlockState(x, localY - 1, z);  // 下
                n[2] = getLocalBlockState(x + 1, localY, z);  // 右
                n[3] = getLocalBlockState(x - 1, localY, z);  // 左
                n[4] = getLocalBlockState(x, localY, z + 1);  // 前
                n[5] = getLocalBlockState(x, localY, z - 1);  // 后

                // ===== 上面 (y+) =====
                bool renderTop = (n[0] == 0) || !isFullBlock(n[0]);
                bool isSnowCovered = (blockMeta.isGrassBlock && n[0] != 0 && registry.getBlockMetadata(n[0]).isSnow);
                int sideTexIndex = isSnowCovered ? TEX_GRASS_BLOCK_SNOW : tex.side;
                if (renderTop) {
                    float texIndex = static_cast<float>(tex.top);
                    vertices.push_back({{posX, posY + blockHeight, posZ + 1.0f}, {0.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ + 1.0f}, {1.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ}, {1.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX, posY + blockHeight, posZ}, {0.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase); indices.push_back(faceBase + 1); indices.push_back(faceBase + 2);
                    indices.push_back(faceBase); indices.push_back(faceBase + 2); indices.push_back(faceBase + 3);
                }

                // ===== 下面 (y-) =====
                if (isFullBlockHeight && (n[1] == 0 || !isFullBlock(n[1]))) {
                    float texIndex = static_cast<float>(tex.bottom);
                    vertices.push_back({{posX, posY, posZ}, {0.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 1.0f, posY, posZ}, {1.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 1.0f, posY, posZ + 1.0f}, {1.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX, posY, posZ + 1.0f}, {0.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase); indices.push_back(faceBase + 1); indices.push_back(faceBase + 2);
                    indices.push_back(faceBase); indices.push_back(faceBase + 2); indices.push_back(faceBase + 3);
                }

                // ===== 草方块侧面覆盖层（overlay）准备 =====
                float baseSideTexIndex;
                uint8_t baseSideColor[4];
                bool needsOverlay = false;
                if (blockMeta.isGrassBlock) {
                    if (isSnowCovered) {
                        baseSideTexIndex = static_cast<float>(TEX_GRASS_BLOCK_SNOW);
                        baseSideColor[0] = 255; baseSideColor[1] = 255; baseSideColor[2] = 255; baseSideColor[3] = 255;
                    } else {
                        baseSideTexIndex = static_cast<float>(TEX_GRASS_SIDE);
                        baseSideColor[0] = 255; baseSideColor[1] = 255; baseSideColor[2] = 255; baseSideColor[3] = 255;
                        needsOverlay = true;
                    }
                } else {
                    baseSideTexIndex = static_cast<float>(sideTexIndex);
                    baseSideColor[0] = tintR; baseSideColor[1] = tintG; baseSideColor[2] = tintB; baseSideColor[3] = 255;
                }

                auto addOverlaySide = [&](float x1, float y1, float z1,
                                          float x2, float y2, float z2,
                                          float x3, float y3, float z3,
                                          float x4, float y4, float z4) {
                    float overlayTex = static_cast<float>(TEX_GRASS_SIDE_OVERLAY);
                    uint32_t ob = static_cast<uint32_t>(overlayVertices.size());
                    overlayVertices.push_back({{x1, y1, z1}, {0.0f, 1.0f}, overlayTex, {tintR, tintG, tintB, 255}});
                    overlayVertices.push_back({{x2, y2, z2}, {1.0f, 1.0f}, overlayTex, {tintR, tintG, tintB, 255}});
                    overlayVertices.push_back({{x3, y3, z3}, {1.0f, 0.0f}, overlayTex, {tintR, tintG, tintB, 255}});
                    overlayVertices.push_back({{x4, y4, z4}, {0.0f, 0.0f}, overlayTex, {tintR, tintG, tintB, 255}});
                    overlayIndices.push_back(ob); overlayIndices.push_back(ob + 1); overlayIndices.push_back(ob + 2);
                    overlayIndices.push_back(ob); overlayIndices.push_back(ob + 2); overlayIndices.push_back(ob + 3);
                    overlayIndexCount += 6;
                };

                // ===== 四个侧面 =====
                // 前面 (z+)
                if (n[4] == 0 || !isFullBlock(n[4])) {
                    vertices.push_back({{posX, posY, posZ + 1.0f}, {0.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY, posZ + 1.0f}, {1.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ + 1.0f}, {1.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY + blockHeight, posZ + 1.0f}, {0.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase); indices.push_back(faceBase + 1); indices.push_back(faceBase + 2);
                    indices.push_back(faceBase); indices.push_back(faceBase + 2); indices.push_back(faceBase + 3);
                    if (needsOverlay) addOverlaySide(posX, posY, posZ + 1.0f, posX + 1.0f, posY, posZ + 1.0f,
                                                     posX + 1.0f, posY + blockHeight, posZ + 1.0f,
                                                     posX, posY + blockHeight, posZ + 1.0f);
                }
                // 后面 (z-)
                if (n[5] == 0 || !isFullBlock(n[5])) {
                    vertices.push_back({{posX + 1.0f, posY, posZ}, {0.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY, posZ}, {1.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY + blockHeight, posZ}, {1.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ}, {0.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase); indices.push_back(faceBase + 1); indices.push_back(faceBase + 2);
                    indices.push_back(faceBase); indices.push_back(faceBase + 2); indices.push_back(faceBase + 3);
                    if (needsOverlay) addOverlaySide(posX + 1.0f, posY, posZ, posX, posY, posZ,
                                                     posX, posY + blockHeight, posZ,
                                                     posX + 1.0f, posY + blockHeight, posZ);
                }
                // 右面 (x+)
                if (n[2] == 0 || !isFullBlock(n[2])) {
                    vertices.push_back({{posX + 1.0f, posY, posZ + 1.0f}, {0.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY, posZ}, {1.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ}, {1.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ + 1.0f}, {0.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase); indices.push_back(faceBase + 1); indices.push_back(faceBase + 2);
                    indices.push_back(faceBase); indices.push_back(faceBase + 2); indices.push_back(faceBase + 3);
                    if (needsOverlay) addOverlaySide(posX + 1.0f, posY, posZ + 1.0f, posX + 1.0f, posY, posZ,
                                                     posX + 1.0f, posY + blockHeight, posZ,
                                                     posX + 1.0f, posY + blockHeight, posZ + 1.0f);
                }
                // 左面 (x-)
                if (n[3] == 0 || !isFullBlock(n[3])) {
                    vertices.push_back({{posX, posY, posZ}, {0.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY, posZ + 1.0f}, {1.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY + blockHeight, posZ + 1.0f}, {1.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY + blockHeight, posZ}, {0.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase); indices.push_back(faceBase + 1); indices.push_back(faceBase + 2);
                    indices.push_back(faceBase); indices.push_back(faceBase + 2); indices.push_back(faceBase + 3);
                    if (needsOverlay) addOverlaySide(posX, posY, posZ, posX, posY, posZ + 1.0f,
                                                     posX, posY + blockHeight, posZ + 1.0f,
                                                     posX, posY + blockHeight, posZ);
                }
            }
        }
    }
    
    // 合并 overlay 几何体到主缓冲区末尾（所有 base 在前，overlay 在后）
    if (!overlayIndices.empty()) {
        uint32_t baseVertexCount = static_cast<uint32_t>(vertices.size());
        for (auto& idx : overlayIndices) {
            idx += baseVertexCount;
        }
        vertices.insert(vertices.end(), overlayVertices.begin(), overlayVertices.end());
        indices.insert(indices.end(), overlayIndices.begin(), overlayIndices.end());
    }

    return {std::move(vertices), std::move(indices), overlayIndexCount};
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