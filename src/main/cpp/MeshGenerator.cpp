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

// 面索引
enum Face : int { TOP = 0, BOTTOM, RIGHT, LEFT, FRONT, BACK };

// ===== 面顶点模板 =====
// 每个面 4 个顶点: {ox, oy, oz, u, v}，相对 (0,0,0) 偏移，范围 [0,1]
// 最终坐标: pos + offset * scale，其中 scale = (1, blockHeight, 1)
static const float FACE_VERTS[6][4][5] = {
    {{0,1,1,0,0},{1,1,1,1,0},{1,1,0,1,1},{0,1,0,0,1}}, // TOP
    {{0,0,0,0,1},{1,0,0,1,1},{1,0,1,1,0},{0,0,1,0,0}}, // BOTTOM
    {{1,0,1,0,1},{1,0,0,1,1},{1,1,0,1,0},{1,1,1,0,0}}, // RIGHT
    {{0,0,0,0,1},{0,0,1,1,1},{0,1,1,1,0},{0,1,0,0,0}}, // LEFT
    {{0,0,1,0,1},{1,0,1,1,1},{1,1,1,1,0},{0,1,1,0,0}}, // FRONT
    {{1,0,0,0,1},{0,0,0,1,1},{0,1,0,1,0},{1,1,0,0,0}}, // BACK
};

// ===== 通用方块面生成 =====
static void addCubicFace(
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
    int face,
    float px, float py, float pz, float height,
    float texIndex,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint32_t base = static_cast<uint32_t>(vertices.size());
    for (int v = 0; v < 4; v++) {
        const float* fv = FACE_VERTS[face][v];
        Vertex vert;
        vert.pos[0] = px + fv[0];
        vert.pos[1] = py + fv[1] * height;
        vert.pos[2] = pz + fv[2];
        vert.texCoord[0] = fv[3];
        vert.texCoord[1] = fv[4];
        vert.texIndex = texIndex;
        vert.color[0] = r; vert.color[1] = g; vert.color[2] = b; vert.color[3] = a;
        vertices.push_back(vert);
    }
    indices.push_back(base);     indices.push_back(base + 1); indices.push_back(base + 2);
    indices.push_back(base);     indices.push_back(base + 2); indices.push_back(base + 3);
}

// 辅助函数：获取全局坐标的 blockState
int32_t MeshGenerator::getBlockStateAt(int x, int y, int z, const ChunkManager* chunkManager) {
    if (!chunkManager) return 0;

    int chunkX = (int)floor((float)x / 16.0f);
    int chunkZ = (int)floor((float)z / 16.0f);
    int localX = x - chunkX * 16;
    int localZ = z - chunkZ * 16;

    if (localX < 0 || localX >= 16 || localZ < 0 || localZ >= 16) {
        LOGE("Invalid local coordinates: x=%d, z=%d, localX=%d, localZ=%d", x, z, localX, localZ);
        return 0;
    }

    auto chunk = chunkManager->getChunk(chunkX, chunkZ);
    if (!chunk || !chunk->isLoaded) return 0;

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
    return 0;
}

std::vector<Vertex> MeshGenerator::generateMesh(const Chunk& chunk) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    for (size_t sectionIdx = 0; sectionIdx < chunk.sections.size(); ++sectionIdx) {
        const auto& section = chunk.sections[sectionIdx];
        if (!section || section->isEmpty) continue;
        int sectionY = section->y;
        auto meshOut = generateSectionMesh(*section, chunk.pos.x, sectionY, chunk.pos.z, nullptr);
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

    for (size_t sectionIdx = 0; sectionIdx < chunk.sections.size(); ++sectionIdx) {
        const auto& section = chunk.sections[sectionIdx];
        if (!section || section->isEmpty) continue;
        int sectionY = section->y;
        auto meshOut = generateSectionMesh(*section, chunk.pos.x, sectionY, chunk.pos.z, nullptr);
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
    std::vector<Vertex> baseVertices;
    std::vector<uint32_t> baseIndices;
    std::vector<Vertex> overlayVertices;
    std::vector<uint32_t> overlayIndices;
    std::vector<Vertex> waterVertices;
    std::vector<uint32_t> waterIndices;
    baseVertices.reserve(20000);
    baseIndices.reserve(30000);
    overlayVertices.reserve(4000);
    overlayIndices.reserve(6000);
    waterVertices.reserve(2000);
    waterIndices.reserve(3000);
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

    static thread_local std::unordered_map<int32_t, bool> solidCache;

    auto isSolid = [&](int32_t state) -> bool {
        if (state == 0) return false;
        auto it = solidCache.find(state);
        if (it != solidCache.end()) return it->second;
        bool solid = BlockRegistry::getInstance().getBlockMetadata(state).isFullBlock;
        solidCache[state] = solid;
        return solid;
    };

// 在函数末尾（return 之前）打印统计

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

                // 生物群系颜色
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
                bool isFullBlockHeight = blockHeight >= 1.0f;

                float posX = baseX + x;
                float posY = baseY + localY;
                float posZ = baseZ + z;

                // 6 个邻居
                int32_t n[6];
                if (x >= 1 && x <= 14 && localY >= 1 && localY <= 14 && z >= 1 && z <= 14) {
                    // 内部方块：所有邻居都在当前 section 内，直接索引偏移
                    n[TOP]    = selfData[index + 256];
                    n[BOTTOM] = selfData[index - 256];
                    n[RIGHT]  = selfData[index + 1];
                    n[LEFT]   = selfData[index - 1];
                    n[FRONT]  = selfData[index + 16];
                    n[BACK]   = selfData[index - 16];
                } else {
                    n[TOP]    = getLocalBlockState(x, localY + 1, z);
                    n[BOTTOM] = getLocalBlockState(x, localY - 1, z);
                    n[RIGHT]  = getLocalBlockState(x + 1, localY, z);
                    n[LEFT]   = getLocalBlockState(x - 1, localY, z);
                    n[FRONT]  = getLocalBlockState(x, localY, z + 1);
                    n[BACK]   = getLocalBlockState(x, localY, z - 1);
                }

                // ===== 植物 =====
                if (blockMeta.isPlant) {
                    uint8_t plantR = 255, plantG = 255, plantB = 255;
                    BiomeColorManager::getInstance().getGrassColor(biomeId, plantR, plantG, plantB);

                    float plantTexIndex = static_cast<float>(tex.top);
                    uint32_t baseIdx = static_cast<uint32_t>(baseVertices.size());

                    // 四边形 1 (沿 z 轴)
                    baseVertices.push_back({{posX, posY, posZ + 0.5f}, {0.0f, 1.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});
                    baseVertices.push_back({{posX + 1.0f, posY, posZ + 0.5f}, {1.0f, 1.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});
                    baseVertices.push_back({{posX + 1.0f, posY + 1.0f, posZ + 0.5f}, {1.0f, 0.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});
                    baseVertices.push_back({{posX, posY + 1.0f, posZ + 0.5f}, {0.0f, 0.0f}, plantTexIndex, {plantR, plantG, plantB, 255}});
                    baseIndices.push_back(baseIdx); baseIndices.push_back(baseIdx + 1); baseIndices.push_back(baseIdx + 2);
                    baseIndices.push_back(baseIdx); baseIndices.push_back(baseIdx + 2); baseIndices.push_back(baseIdx + 3);
                    baseIndices.push_back(baseIdx + 2); baseIndices.push_back(baseIdx + 1); baseIndices.push_back(baseIdx + 0);
                    baseIndices.push_back(baseIdx + 3); baseIndices.push_back(baseIdx + 2); baseIndices.push_back(baseIdx + 0);

                    baseIdx += 4;

                    // 四边形 2 (沿 x 轴)
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

                    // 顶面
                    if (!isWaterBlock(n[TOP])) {
                        addCubicFace(waterVertices, waterIndices, TOP,
                                     posX, posY, posZ, blockHeight, waterTexIndex, waterR, waterG, waterB, 180);
                    }
                    // 4 个侧面（FRONT, BACK, RIGHT, LEFT = 索引 4,5,2,3，对应 n[face]）
                    static const int WATER_SIDES[] = {FRONT, BACK, RIGHT, LEFT};
                    for (int ws : WATER_SIDES) {
                        if (!isWaterBlock(n[ws]) && (n[ws] == 0 || !isSolid(n[ws]))) {
                            addCubicFace(waterVertices, waterIndices, ws,
                                         posX, posY, posZ, blockHeight, waterTexIndex, waterR, waterG, waterB, 180);
                        }
                    }
                    continue;
                }

                // ===== 普通不透明方块 =====
                bool renderTop = (n[TOP] == 0) || !isSolid(n[TOP]);
                bool isSnowCovered = (blockMeta.isGrassBlock && n[TOP] != 0 &&
                                      BlockRegistry::getInstance().getBlockMetadata(n[TOP]).isSnow);
                bool isGrassSide = blockMeta.isGrassBlock && !isSnowCovered;

                // 顶面
                if (renderTop) {
                    float topTex = isSnowCovered
                        ? static_cast<float>(TEX_GRASS_BLOCK_SNOW)
                        : static_cast<float>(tex.top);
                    addCubicFace(baseVertices, baseIndices, TOP,
                                 posX, posY, posZ, blockHeight, topTex, tintR, tintG, tintB, 255);
                }

                // 底面
                if (isFullBlockHeight && (n[BOTTOM] == 0 || !isSolid(n[BOTTOM]))) {
                    addCubicFace(baseVertices, baseIndices, BOTTOM,
                                 posX, posY, posZ, blockHeight,
                                 static_cast<float>(tex.bottom), tintR, tintG, tintB, 255);
                }

                // 4 个侧面 (FRONT, BACK, RIGHT, LEFT → n[face])
                static const int SIDE_FACES[] = {FRONT, BACK, RIGHT, LEFT};
                for (int sf : SIDE_FACES) {
                    if (n[sf] != 0 && isSolid(n[sf])) continue;

                    float sideTex;
                    uint8_t sr = 255, sg = 255, sb = 255;
                    bool needsOverlay = false;

                    if (isGrassSide) {
                        sideTex = static_cast<float>(TEX_GRASS_SIDE);
                        needsOverlay = true;
                    } else if (isSnowCovered) {
                        sideTex = static_cast<float>(TEX_GRASS_BLOCK_SNOW);
                    } else {
                        sideTex = static_cast<float>(tex.side);
                        sr = tintR; sg = tintG; sb = tintB;
                    }

                    addCubicFace(baseVertices, baseIndices, sf,
                                 posX, posY, posZ, blockHeight, sideTex, sr, sg, sb, 255);

                    if (needsOverlay) {
                        addCubicFace(overlayVertices, overlayIndices, sf,
                                     posX, posY, posZ, blockHeight,
                                     static_cast<float>(TEX_GRASS_SIDE_OVERLAY),
                                     tintR, tintG, tintB, 255);
                    }
                }
            }
        }
    }

    // ===== 合并：base → grass_overlay → water =====
    std::vector<Vertex> allVertices;
    std::vector<uint32_t> allIndices;

    uint32_t baseVertexCount = static_cast<uint32_t>(baseVertices.size());
    allVertices.insert(allVertices.end(), baseVertices.begin(), baseVertices.end());
    allIndices.insert(allIndices.end(), baseIndices.begin(), baseIndices.end());

    if (!overlayVertices.empty()) {
        uint32_t overlayStart = static_cast<uint32_t>(allVertices.size());
        for (uint32_t idx : overlayIndices) {
            allIndices.push_back(overlayStart + idx);
        }
        allVertices.insert(allVertices.end(), overlayVertices.begin(), overlayVertices.end());
    }
    uint32_t overlayIndexCount = static_cast<uint32_t>(overlayIndices.size());

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

