#include "MeshGenerator.h"
#include "BlockRegistry.h"
#include <android/log.h>
#include <map>
#include <string>

#define LOG_TAG "MeshGenerator"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

std::vector<Vertex> MeshGenerator::generateMesh(const Chunk& chunk) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // 使用动态的 sections 大小
    for (size_t sectionIdx = 0; sectionIdx < chunk.sections.size(); ++sectionIdx) {
        const auto& section = chunk.sections[sectionIdx];
        if (!section || section->isEmpty) continue;
        
        int sectionY = section->y;
        auto [sectionVertices, sectionIndices] = generateSectionMesh(*section, chunk.pos.x, sectionY, chunk.pos.z);
        
        // 调整索引偏移
        uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
        for (uint32_t idx : sectionIndices) {
            indices.push_back(vertexOffset + idx);
        }
        
        vertices.insert(vertices.end(), sectionVertices.begin(), sectionVertices.end());
    }
    
    // 注意：这里只返回顶点，索引需要在调用处合并
    // 为了简化，我们直接在这里生成完整的顶点列表（不含索引）
    return vertices;
}

std::pair<std::vector<Vertex>, std::vector<uint32_t>> MeshGenerator::generateMeshWithIndices(const Chunk& chunk) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // 使用动态的 sections 大小
    for (size_t sectionIdx = 0; sectionIdx < chunk.sections.size(); ++sectionIdx) {
        const auto& section = chunk.sections[sectionIdx];
        if (!section || section->isEmpty) continue;
        
        int sectionY = section->y;
        auto [sectionVertices, sectionIndices] = generateSectionMesh(*section, chunk.pos.x, sectionY, chunk.pos.z);
        
        // 调整索引偏移
        uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());
        for (uint32_t idx : sectionIndices) {
            indices.push_back(vertexOffset + idx);
        }
        
        vertices.insert(vertices.end(), sectionVertices.begin(), sectionVertices.end());
    }
    
    return {vertices, indices};
}

std::pair<std::vector<Vertex>, std::vector<uint32_t>> MeshGenerator::generateSectionMesh(const ChunkSection& section, 
                                                        int chunkX, int sectionY, int chunkZ) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    float baseX = chunkX * CHUNK_WIDTH;
    float baseY = static_cast<float>(sectionY);  // sectionY 已经是绝对坐标
    float baseZ = chunkZ * CHUNK_DEPTH;
    
    int blocksRendered = 0;
    int grassBlocksRendered = 0;
    
    // 统计 texIndex 分布
    int texIndexCount[3] = {0, 0, 0};  // layer 0, 1, 2 的数量
    
    // 统计 blockState ID 分布（用于调试）
    std::map<int32_t, int> blockStateCount;
    
    // 辅助函数：检查某个位置是否是空气
    auto isAir = [&](int x, int y, int z) -> bool {
        if (x < 0 || x >= CHUNK_WIDTH || y < 0 || y >= SECTION_HEIGHT || z < 0 || z >= CHUNK_DEPTH) {
            return true;  // 超出边界视为空气（简化处理）
        }
        int idx = (y * CHUNK_DEPTH + z) * CHUNK_WIDTH + x;
        if (idx >= static_cast<int>(section.blockStates.size())) {
            return true;
        }
        return section.blockStates[idx] == 0;
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
                
                // 计算方块的绝对坐标
                float posX = baseX + x;
                float posY = baseY + localY;
                float posZ = baseZ + z;
                
                uint32_t baseIndex = static_cast<uint32_t>(vertices.size());
                
                // 检查 6 个方向的相邻方块，只渲染暴露的面
                
                // 上面 (y+) - 如果上方是空气则渲染
                if (isAir(x, localY + 1, z)) {
                    float texIndex = 0.0f;  // 顶部使用 layer 0（grass_top）
                    texIndexCount[0]++;  // 统计 layer 0
                    
                    // 调试日志：TOP 面
                    if (blockState == 2 && x == 8 && z == 8) {
                        LOGI("Grass block TOP at (%.1f, %.1f, %.1f): texIndex=%.1f", posX, posY+1.0f, posZ, texIndex);
                    }
                    
                    vertices.push_back({{posX, posY + 1.0f, posZ + 1.0f}, {0.0f, 0.0f}, texIndex});
                    vertices.push_back({{posX + 1.0f, posY + 1.0f, posZ + 1.0f}, {1.0f, 0.0f}, texIndex});
                    vertices.push_back({{posX + 1.0f, posY + 1.0f, posZ}, {1.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX, posY + 1.0f, posZ}, {0.0f, 1.0f}, texIndex});
                    
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 1);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase + 3);
                }
                
                // 下面 (y-) - 如果下方是空气则渲染
                if (isAir(x, localY - 1, z)) {
                    // 所有方块的底部都使用 layer 2（dirt）
                    float texIndex = 2.0f;
                    
                    // 统计 texIndex
                    texIndexCount[2]++;
                    
                    // 调试日志：BOTTOM 面
                    if (blockState == 2 && x == 8 && z == 8) {
                        LOGI("Grass block BOTTOM at (%.1f, %.1f, %.1f): texIndex=%.1f", posX, posY, posZ, texIndex);
                    }
                    
                    // V 坐标翻转：0→1, 1→0
                    vertices.push_back({{posX, posY, posZ}, {0.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX + 1.0f, posY, posZ}, {1.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX + 1.0f, posY, posZ + 1.0f}, {1.0f, 0.0f}, texIndex});
                    vertices.push_back({{posX, posY, posZ + 1.0f}, {0.0f, 0.0f}, texIndex});
                    
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 1);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase + 3);
                }
                
                // 前面 (z+) - 如果前方是空气则渲染
                if (isAir(x, localY, z + 1)) {
                    // 所有方块的侧面都使用 layer 1（grass_side）
                    float texIndex = 1.0f;
                    
                    // 统计 texIndex
                    texIndexCount[1]++;
                    
                    // 调试日志：SIDE 面
                    if (blockState == 2 && x == 8 && z == 8) {
                        LOGI("Grass block SIDE (front) at (%.1f, %.1f, %.1f): texIndex=%.1f", posX, posY, posZ+1.0f, texIndex);
                    }
                    
                    // V 坐标翻转
                    vertices.push_back({{posX, posY, posZ + 1.0f}, {0.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX + 1.0f, posY, posZ + 1.0f}, {1.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX + 1.0f, posY + 1.0f, posZ + 1.0f}, {1.0f, 0.0f}, texIndex});
                    vertices.push_back({{posX, posY + 1.0f, posZ + 1.0f}, {0.0f, 0.0f}, texIndex});
                    
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 1);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase + 3);
                }
                
                // 后面 (z-) - 如果后方是空气则渲染
                if (isAir(x, localY, z - 1)) {
                    // 所有方块的侧面都使用 layer 1（grass_side）
                    float texIndex = 1.0f;
                    
                    // 统计 texIndex
                    texIndexCount[1]++;
                    
                    // V 坐标翻转
                    vertices.push_back({{posX + 1.0f, posY, posZ}, {0.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX, posY, posZ}, {1.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX, posY + 1.0f, posZ}, {1.0f, 0.0f}, texIndex});
                    vertices.push_back({{posX + 1.0f, posY + 1.0f, posZ}, {0.0f, 0.0f}, texIndex});
                    
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 1);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase + 3);
                }
                
                // 右面 (x+) - 如果右方是空气则渲染
                if (isAir(x + 1, localY, z)) {
                    // 所有方块的侧面都使用 layer 1（grass_side）
                    float texIndex = 1.0f;
                    
                    // 统计 texIndex
                    texIndexCount[1]++;
                    
                    // V 坐标翻转
                    vertices.push_back({{posX + 1.0f, posY, posZ + 1.0f}, {0.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX + 1.0f, posY, posZ}, {1.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX + 1.0f, posY + 1.0f, posZ}, {1.0f, 0.0f}, texIndex});
                    vertices.push_back({{posX + 1.0f, posY + 1.0f, posZ + 1.0f}, {0.0f, 0.0f}, texIndex});
                    
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 1);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase + 3);
                }
                
                // 左面 (x-) - 如果左方是空气则渲染
                if (isAir(x - 1, localY, z)) {
                    // 所有方块的侧面都使用 layer 1（grass_side）
                    float texIndex = 1.0f;
                    
                    // 统计 texIndex
                    texIndexCount[1]++;
                    
                    // V 坐标翻转
                    vertices.push_back({{posX, posY, posZ}, {0.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX, posY, posZ + 1.0f}, {1.0f, 1.0f}, texIndex});
                    vertices.push_back({{posX, posY + 1.0f, posZ + 1.0f}, {1.0f, 0.0f}, texIndex});
                    vertices.push_back({{posX, posY + 1.0f, posZ}, {0.0f, 0.0f}, texIndex});
                    
                    uint32_t faceBase = static_cast<uint32_t>(vertices.size()) - 4;
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 1);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase);
                    indices.push_back(faceBase + 2);
                    indices.push_back(faceBase + 3);
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
    
    return {vertices, indices};
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