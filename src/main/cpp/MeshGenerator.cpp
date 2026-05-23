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
    
    int blocksRendered = 0;
    int grassBlocksRendered = 0;
    
    // 统计 texIndex 分布
    int texIndexCount[3] = {0, 0, 0};  // layer 0, 1, 2 的数量
    
    // 统计 blockState ID 分布（用于调试）
    std::map<int32_t, int> blockStateCount;
    
    // 辅助函数：检查某个位置是否是空气（支持跨区块/跨 Section 查询）
    auto isAir = [&](int x, int y, int z) -> bool {
        // 如果在当前 Section 范围内，直接查询
        if (x >= 0 && x < CHUNK_WIDTH && 
            y >= 0 && y < SECTION_HEIGHT && 
            z >= 0 && z < CHUNK_DEPTH) {
            int idx = (y * CHUNK_DEPTH + z) * CHUNK_WIDTH + x;
            if (idx >= 0 && idx < static_cast<int>(section.blockStates.size())) {
                return section.blockStates[idx] == 0;
            }
            return true;
        }
        
        // 超出当前 Section，需要查询相邻区块/Section
        if (chunkManager) {
            // 计算全局坐标
            int globalX = chunkX * CHUNK_WIDTH + x;
            int globalY = sectionY + y;
            int globalZ = chunkZ * CHUNK_DEPTH + z;
            
            int32_t blockState = getBlockStateAt(globalX, globalY, globalZ, chunkManager);
            


            return blockState == 0;
        }
        
        // 没有 ChunkManager，视为空气
        return true;
    };

    // 获取局部坐标的 blockState（用于非完整方块的面剔除判断）
    auto getLocalBlockState = [&](int x, int y, int z) -> int32_t {
        if (x >= 0 && x < CHUNK_WIDTH &&
            y >= 0 && y < SECTION_HEIGHT &&
            z >= 0 && z < CHUNK_DEPTH) {
            int idx = (y * CHUNK_DEPTH + z) * CHUNK_WIDTH + x;
            if (idx >= 0 && idx < static_cast<int>(section.blockStates.size())) {
                return section.blockStates[idx];
            }
            return 0;
        }
        if (chunkManager) {
            int globalX = chunkX * CHUNK_WIDTH + x;
            int globalY = sectionY + y;
            int globalZ = chunkZ * CHUNK_DEPTH + z;
            return getBlockStateAt(globalX, globalY, globalZ, chunkManager);
        }
        return 0;
    };

    // 判断相邻位置是否非完整方块（用于侧面/底面剔除：仅完整方块能遮挡相邻面）
    auto isNotFullSolid = [&](int x, int y, int z) -> bool {
        int32_t state = getLocalBlockState(x, y, z);
        return state == 0 || !isFullBlock(state);
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
                
                // 统计 blockState ID
                blockStateCount[blockState]++;
                
                // 跳过空气方块 (block state 为 0)
                if (blockState == 0) {
                    continue;
                }
                
                blocksRendered++;

                // ===== 生物群系染色计算 =====
                uint8_t tintR = 255, tintG = 255, tintB = 255;
                auto& registry = BlockRegistry::getInstance();
                std::string blockName = registry.getBlockName(blockState);
                {
                    int biomeIdx = ((localY >> 2) << 4) | ((z >> 2) << 2) | (x >> 2);
                    int32_t biomeId = 0;
                    if (biomeIdx < static_cast<int>(section.biomes.size())) {
                        biomeId = section.biomes[biomeIdx];
                    }
                    if (blockName == "grass_block") {
                        BiomeColorManager::getInstance().getGrassColor(biomeId, tintR, tintG, tintB);
                    } else if (blockName.find("leaves") != std::string::npos) {
                        BiomeColorManager::getInstance().getFoliageColor(biomeId, tintR, tintG, tintB);
                    } else if (blockName == "grass" || blockName == "tall_grass" || blockName == "fern" || blockName == "large_fern") {
                        BiomeColorManager::getInstance().getGrassColor(biomeId, tintR, tintG, tintB);
                    }
                }

                // 查询方块纹理配置（各面的纹理层索引）
                BlockTextureConfig tex = getBlockTexture(blockState);

                // 查询方块高度（完整方块=1.0，雪片<1.0）
                float blockHeight = getBlockHeight(blockState);
                bool isFullBlockHeight = blockHeight >= 1.0f;

                // 计算方块的绝对坐标
                float posX = baseX + x;
                float posY = baseY + localY;
                float posZ = baseZ + z;

                // ===== 植物类方块：十字交叉渲染（两个垂直四边形交叉成 X 形）=====
                if (isPlant(blockState)) {
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

                uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

                // 检查 6 个方向的相邻方块，只渲染暴露的面
                
                // 上面 (y+) - 上方是空气或非完整方块（如透明植物、雪片）时渲染顶面
                int32_t aboveState = getLocalBlockState(x, localY + 1, z);
                bool renderTop = (aboveState == 0) || !isFullBlock(aboveState);

                // ===== 草方块雪覆盖：上方有雪片时侧面使用雪纹理 =====
                bool isSnowCovered = (blockName == "grass_block" && aboveState != 0 && registry.getBlockName(aboveState) == "snow");
                int sideTexIndex = isSnowCovered ? TEX_GRASS_BLOCK_SNOW : tex.side;
                if (renderTop) {
                    float texIndex = static_cast<float>(tex.top);
                    texIndexCount[0]++;  // 统计 layer 0
                    
                    // 调试日志：TOP 面
                    if (blockState == 2 && x == 8 && z == 8) {
                        LOGI("Grass block TOP at (%.1f, %.1f, %.1f): texIndex=%.1f", posX, posY+1.0f, posZ, texIndex);
                    }
                    
                    vertices.push_back({{posX, posY + blockHeight, posZ + 1.0f}, {0.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ + 1.0f}, {1.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ}, {1.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX, posY + blockHeight, posZ}, {0.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 1);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase + 3);
                }
                
                // 下面 (y-) - 仅完整方块渲染，且下方不是完整方块时
                if (isFullBlockHeight && isNotFullSolid(x, localY - 1, z)) {
                    // 根据方块纹理配置查询底面纹理
                    float texIndex = static_cast<float>(tex.bottom);
                    
                    // 统计 texIndex
                    texIndexCount[2]++;
                    
                    // 调试日志：BOTTOM 面
                    if (blockState == 2 && x == 8 && z == 8) {
                        LOGI("Grass block BOTTOM at (%.1f, %.1f, %.1f): texIndex=%.1f", posX, posY, posZ, texIndex);
                    }
                    
                    // V 坐标翻转：0→1, 1→0
                    vertices.push_back({{posX, posY, posZ}, {0.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 1.0f, posY, posZ}, {1.0f, 1.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX + 1.0f, posY, posZ + 1.0f}, {1.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    vertices.push_back({{posX, posY, posZ + 1.0f}, {0.0f, 0.0f}, texIndex, {tintR, tintG, tintB, 255}});
                    
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 1);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase + 3);
                }

                // ===== 草方块侧面覆盖层（overlay）准备 =====
                // 原版 Minecraft 中，草方块侧面由两层组成：
                //   基础层：grass_block_side.png（泥土+草皮底层，不染色）
                //   覆盖层：grass_block_side_overlay.png（顶部草皮层，染色后 alpha blend）
                float baseSideTexIndex;
                uint8_t baseSideColor[4];
                bool needsOverlay = false;

                if (blockName == "grass_block") {
                    if (isSnowCovered) {
                        // 雪覆盖时：使用雪纹理，不染色，无 overlay
                        baseSideTexIndex = static_cast<float>(TEX_GRASS_BLOCK_SNOW);
                        baseSideColor[0] = 255; baseSideColor[1] = 255; baseSideColor[2] = 255; baseSideColor[3] = 255;
                    } else {
                        // 普通草方块：基础层不染色 + overlay 染色
                        baseSideTexIndex = static_cast<float>(TEX_GRASS_SIDE);
                        baseSideColor[0] = 255; baseSideColor[1] = 255; baseSideColor[2] = 255; baseSideColor[3] = 255;
                        needsOverlay = true;
                    }
                } else {
                    // 其他方块：直接使用原有纹理 + tint 染色
                    baseSideTexIndex = static_cast<float>(sideTexIndex);
                    baseSideColor[0] = tintR; baseSideColor[1] = tintG; baseSideColor[2] = tintB; baseSideColor[3] = 255;
                }

                // 辅助 lambda：为草方块生成一层 overlay 侧面（4 顶点 + 6 索引）
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

                    overlayIndices.push_back(ob);
                    overlayIndices.push_back(ob + 1);
                    overlayIndices.push_back(ob + 2);
                    overlayIndices.push_back(ob);
                    overlayIndices.push_back(ob + 2);
                    overlayIndices.push_back(ob + 3);

                    overlayIndexCount += 6;
                };

                // 前面 (z+) - 邻居非完整方块时渲染（深度测试处理交叠）
                if (isNotFullSolid(x, localY, z + 1)) {
                    texIndexCount[1]++;
                    vertices.push_back({{posX, posY, posZ + 1.0f}, {0.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY, posZ + 1.0f}, {1.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ + 1.0f}, {1.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY + blockHeight, posZ + 1.0f}, {0.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase); indices.push_back(faceBase + 1); indices.push_back(faceBase + 2);
                    indices.push_back(faceBase); indices.push_back(faceBase + 2); indices.push_back(faceBase + 3);

                    if (needsOverlay) {
                        addOverlaySide(posX, posY, posZ + 1.0f, posX + 1.0f, posY, posZ + 1.0f,
                                       posX + 1.0f, posY + blockHeight, posZ + 1.0f,
                                       posX, posY + blockHeight, posZ + 1.0f);
                    }
                }
                
                // 后面 (z-) - 邻居非完整方块时渲染（深度测试处理交叠）
                if (isNotFullSolid(x, localY, z - 1)) {
                    texIndexCount[1]++;
                    vertices.push_back({{posX + 1.0f, posY, posZ}, {0.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY, posZ}, {1.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY + blockHeight, posZ}, {1.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ}, {0.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase); indices.push_back(faceBase + 1); indices.push_back(faceBase + 2);
                    indices.push_back(faceBase); indices.push_back(faceBase + 2); indices.push_back(faceBase + 3);

                    if (needsOverlay) {
                        addOverlaySide(posX + 1.0f, posY, posZ, posX, posY, posZ,
                                       posX, posY + blockHeight, posZ,
                                       posX + 1.0f, posY + blockHeight, posZ);
                    }
                }
                
                // 右面 (x+) - 邻居非完整方块时渲染（深度测试处理交叠）
                if (isNotFullSolid(x + 1, localY, z)) {
                    texIndexCount[1]++;
                    vertices.push_back({{posX + 1.0f, posY, posZ + 1.0f}, {0.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY, posZ}, {1.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ}, {1.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX + 1.0f, posY + blockHeight, posZ + 1.0f}, {0.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase); indices.push_back(faceBase + 1); indices.push_back(faceBase + 2);
                    indices.push_back(faceBase); indices.push_back(faceBase + 2); indices.push_back(faceBase + 3);

                    if (needsOverlay) {
                        addOverlaySide(posX + 1.0f, posY, posZ + 1.0f, posX + 1.0f, posY, posZ,
                                       posX + 1.0f, posY + blockHeight, posZ,
                                       posX + 1.0f, posY + blockHeight, posZ + 1.0f);
                    }
                }
                
                // 左面 (x-) - 邻居非完整方块时渲染（深度测试处理交叠）
                if (isNotFullSolid(x - 1, localY, z)) {
                    texIndexCount[1]++;
                    vertices.push_back({{posX, posY, posZ}, {0.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY, posZ + 1.0f}, {1.0f, 1.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY + blockHeight, posZ + 1.0f}, {1.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    vertices.push_back({{posX, posY + blockHeight, posZ}, {0.0f, 0.0f}, baseSideTexIndex, {baseSideColor[0], baseSideColor[1], baseSideColor[2], baseSideColor[3]}});
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase); indices.push_back(faceBase + 1); indices.push_back(faceBase + 2);
                    indices.push_back(faceBase); indices.push_back(faceBase + 2); indices.push_back(faceBase + 3);

                    if (needsOverlay) {
                        addOverlaySide(posX, posY, posZ, posX, posY, posZ + 1.0f,
                                       posX, posY + blockHeight, posZ + 1.0f,
                                       posX, posY + blockHeight, posZ);
                    }
                }
            }
        }
    }
    
    if (blocksRendered > 0) {
        LOGI("Section (%d, %d, %d): %d blocks rendered, %d grass blocks", 
             chunkX, sectionY, chunkZ, blocksRendered, grassBlocksRendered);
        LOGI("  texIndex distribution: layer0=%d, layer1=%d, layer2=%d",
             texIndexCount[0], texIndexCount[1], texIndexCount[2]);
        
        // 打印 blockState ID 分布（前 10 个最多的）
        if (BlockRegistry::getInstance().isLoaded()) {
            LOGI("  BlockState ID distribution:");
            int count = 0;
            for (auto it = blockStateCount.rbegin(); it != blockStateCount.rend() && count < 10; ++it, ++count) {
                std::string blockName = BlockRegistry::getInstance().getBlockName(it->first);
                LOGI("    blockState=%d (%s): %d blocks", it->first, blockName.c_str(), it->second);
            }
        } else {
            LOGE("  BlockRegistry not loaded, skipping block name lookup");
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