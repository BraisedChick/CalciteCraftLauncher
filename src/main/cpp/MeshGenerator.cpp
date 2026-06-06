#include "MeshGenerator.h"
#include "BlockRegistry.h"
#include "TextureAtlas.h"
#include "BiomeColorManager.h"
#include <android/log.h>
#include <map>
#include <string>
#include <cmath>

// 添加 GLM（用于模型元素旋转）
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define LOG_TAG "MeshGenerator"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 面索引
enum Face : int { TOP = 0, BOTTOM, RIGHT, LEFT, FRONT, BACK };

// 模型 FaceDir → Face 映射表（用于 FV 顶点模板索引和 cullface 邻居查询）
// FaceDir:   DOWN=0, UP=1, NORTH=2, SOUTH=3, WEST=4, EAST=5
// Face:      TOP=0, BOTTOM=1, RIGHT=2, LEFT=3, FRONT=4, BACK=5
// DOWN→BOTTOM(1), UP→TOP(0), NORTH→BACK(5), SOUTH→FRONT(4), WEST→LEFT(3), EAST→RIGHT(2)
static const int8_t FACEDIR_TO_FACE[6] = {1, 0, 5, 4, 3, 2};

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

// ===== 模型驱动渲染辅助函数 =====

// 绕轴旋转点（在元素 0-16 坐标空间中）
static void rotateVertex(float& x, float& y, float& z,
                          const ElementRotation& rot) {
    if (rot.angle == 0.0f) return;

    // 平移到旋转原点
    float dx = x - rot.origin[0];
    float dy = y - rot.origin[1];
    float dz = z - rot.origin[2];

    float rad = rot.angle * (3.14159265f / 180.0f);
    float cosA = cosf(rad);
    float sinA = sinf(rad);

    float nx, ny, nz;
    switch (rot.axis) {
        case 0: // X 轴
            ny = dy * cosA - dz * sinA;
            nz = dy * sinA + dz * cosA;
            nx = dx;
            break;
        case 1: // Y 轴
            nx = dx * cosA + dz * sinA;
            nz = -dx * sinA + dz * cosA;
            ny = dy;
            break;
        case 2: // Z 轴
            nx = dx * cosA - dy * sinA;
            ny = dx * sinA + dy * cosA;
            nz = dz;
            break;
        default:
            return;
    }

    x = rot.origin[0] + nx;
    y = rot.origin[1] + ny;
    z = rot.origin[2] + nz;
}

// 根据模型元素 elements 生成顶点
// vertices/indices: 输出到 base 几何体
// overlayVertices/overlayIndices: 输出草覆盖层几何体
// isGrassBlock, isSnowCovered, grassSideLayer, grassOverlayLayer 用于草地覆盖层特判
static void generateFromModel(
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
    std::vector<Vertex>& overlayVertices, std::vector<uint32_t>& overlayIndices,
    const ResolvedBlockModel& model,
    float blockX, float blockY, float blockZ,  // 方块世界坐标
    const int32_t neighborStates[6],           // 6 个方向的邻居 blockState
    bool isGrassBlock, bool isSnowCovered,
    float grassSideLayer, float grassOverlayLayer,
    uint8_t tintR, uint8_t tintG, uint8_t tintB,
    int biomeId,
    const std::unordered_map<int32_t, bool>* solidCache,
    int bsRotX, int bsRotY) {

    // 用于 face 面剔除检测（优先使用外部缓存，避免重复 getBlockMetadata）
    auto isNeighborSolid = [solidCache](int32_t state) -> bool {
        if (state == 0) return false;
        if (solidCache) {
            auto it = solidCache->find(state);
            if (it != solidCache->end()) return it->second;
        }
        return BlockRegistry::getInstance().getBlockMetadata(state).isFullBlock;
    };

    for (const auto& elem : model.elements) {
        float ew = elem.to[0] - elem.from[0];
        float eh = elem.to[1] - elem.from[1];
        float ed = elem.to[2] - elem.from[2];
        float fx = elem.from[0], fy = elem.from[1], fz = elem.from[2];

        for (int dir = 0; dir < 6; dir++) {
            if (!elem.hasFaces[dir]) continue;

            const ModelFaceData& face = elem.faces[dir];

            // cullface 剔除：检查 cull 方向的邻居
            // FaceDir 枚举值 ≠ Face 枚举值，用 FACEDIR_TO_FACE 映射
            if (face.cullface >= 0 && face.cullface < 6) {
                int32_t neighbor = neighborStates[FACEDIR_TO_FACE[face.cullface]];
                if (neighbor != 0 && isNeighborSolid(neighbor)) {
                    continue; // 被邻居遮挡，跳过该面
                }
            }

            // 确定该面的顶点 UV 和坐标缩放
            // 使用 FACE_VERTS 作为模板，但映射到元素尺寸
            // FACE_VERTS 定义: {ox, oy, oz, u, v} 在 0-1 范围

            uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
            uint32_t overlayBase = static_cast<uint32_t>(overlayVertices.size());

            // UV 范围 (0-1)
            float u1 = face.uv[0] / 16.0f;
            float v1 = face.uv[1] / 16.0f;
            float u2 = face.uv[2] / 16.0f;
            float v2 = face.uv[3] / 16.0f;

            // 根据面方向生成 4 个顶点
            // dir 是 FaceDir 枚举值(DOWN=0,UP=1,NORTH=2,SOUTH=3,WEST=4,EAST=5)，
            // 用 FACEDIR_TO_FACE 映射到 FV 数组的 Face 枚举索引
            static const float FV[6][4][5] = {
                {{0,1,1,0,0},{1,1,1,1,0},{1,1,0,1,1},{0,1,0,0,1}}, // TOP (0)
                {{0,0,0,0,1},{1,0,0,1,1},{1,0,1,1,0},{0,0,1,0,0}}, // BOTTOM (1)
                {{1,0,1,0,1},{1,0,0,1,1},{1,1,0,1,0},{1,1,1,0,0}}, // RIGHT / EAST (2)
                {{0,0,0,0,1},{0,0,1,1,1},{0,1,1,1,0},{0,1,0,0,0}}, // LEFT / WEST (3)
                {{0,0,1,0,1},{1,0,1,1,1},{1,1,1,1,0},{0,1,1,0,0}}, // FRONT / NORTH (4)
                {{1,0,0,0,1},{0,0,0,1,1},{0,1,0,1,0},{1,1,0,0,0}}, // BACK / SOUTH (5)
            };
            int fvIndex = FACEDIR_TO_FACE[dir];

            // 是否需要草覆盖层（grass block side overlay 的特判）
            bool needsOverlay = false;
            float texLayer = static_cast<float>(face.textureLayer);

            // 草地特判：如果是 grass_block 的 side 面且不是雪覆盖
            if (isGrassBlock && !isSnowCovered) {
                // 侧边四个面（不是顶/底）
                if (dir == FACE_NORTH || dir == FACE_SOUTH ||
                    dir == FACE_WEST || dir == FACE_EAST) {
                    // 检查纹理是否为 grass_side，若是则覆盖为 grass_side + overlay
                    // 这里简化处理：草地侧面固定使用 grassSideLayer 和 overlay
                    // 因为 grass_block 的 side 纹理总是 grass_block_side
                    needsOverlay = true;
                    texLayer = grassSideLayer;
                }
            } else if (isSnowCovered && dir == FACE_UP) {
                // 雪覆盖的草地顶面
                texLayer = static_cast<float>(TextureAtlas::getInstance().getGrassBlockSnowLayer());
            }

            // 确定面的颜色（所有 4 个顶点相同，提出到循环外）
            uint8_t cr = 255, cg = 255, cb = 255;
            if (face.tintindex == 0) {
                BiomeColorManager::getInstance().getGrassColor(biomeId, cr, cg, cb);
            } else if (face.tintindex == 1) {
                BiomeColorManager::getInstance().getFoliageColor(biomeId, cr, cg, cb);
            } else if (needsOverlay) {
                // overlay 面：base 面白色，颜色由 overlay 携带
                cr = 255; cg = 255; cb = 255;
            } else if (tintR != 255 || tintG != 255 || tintB != 255) {
                cr = tintR; cg = tintG; cb = tintB;
            }

            // 批量生成 4 个顶点到局部数组，一次性 insert
            Vertex faceVerts[4];
            for (int v = 0; v < 4; v++) {
                const float* fv = FV[fvIndex][v];

                float lx = fx + fv[0] * ew;
                float ly = fy + fv[1] * eh;
                float lz = fz + fv[2] * ed;

                if (elem.rotation.angle != 0.0f) {
                    rotateVertex(lx, ly, lz, elem.rotation);
                }

                // Blockstate 旋转（整个模型绕方块中心 8,8,8）
                if (bsRotX != 0 || bsRotY != 0) {
                    float cx = lx - 8.0f, cy = ly - 8.0f, cz = lz - 8.0f;
                    if (bsRotX != 0) {
                        float rad = bsRotX * (3.14159265f / 180.0f);
                        float cosA = cosf(rad), sinA = sinf(rad);
                        float ny = cy * cosA - cz * sinA;
                        float nz = cy * sinA + cz * cosA;
                        cy = ny; cz = nz;
                    }
                    if (bsRotY != 0) {
                        float rad = bsRotY * (3.14159265f / 180.0f);
                        float cosA = cosf(rad), sinA = sinf(rad);
                        float nx = cx * cosA + cz * sinA;
                        float nz = -cx * sinA + cz * cosA;
                        cx = nx; cz = nz;
                    }
                    lx = cx + 8.0f;
                    ly = cy + 8.0f;
                    lz = cz + 8.0f;
                }

                faceVerts[v].pos[0] = blockX + lx / 16.0f;
                faceVerts[v].pos[1] = blockY + ly / 16.0f;
                faceVerts[v].pos[2] = blockZ + lz / 16.0f;
                faceVerts[v].texCoord[0] = u1 + fv[3] * (u2 - u1);
                faceVerts[v].texCoord[1] = v1 + fv[4] * (v2 - v1);
                faceVerts[v].texIndex = texLayer;
                faceVerts[v].color[0] = cr;
                faceVerts[v].color[1] = cg;
                faceVerts[v].color[2] = cb;
                faceVerts[v].color[3] = 255;
            }
            vertices.insert(vertices.end(), faceVerts, faceVerts + 4);

            // 两个三角形 (CCW)，批量插入
            uint32_t triIdx[6] = {
                baseIdx, baseIdx + 1, baseIdx + 2,
                baseIdx, baseIdx + 2, baseIdx + 3
            };
            indices.insert(indices.end(), triIdx, triIdx + 6);

            // 草覆盖层（额外的半透明 overlay 四边形）
            if (needsOverlay) {
                Vertex overlayVerts[4];
                for (int v = 0; v < 4; v++) {
                    overlayVerts[v] = faceVerts[v];
                    overlayVerts[v].texIndex = grassOverlayLayer;
                    overlayVerts[v].color[0] = tintR;
                    overlayVerts[v].color[1] = tintG;
                    overlayVerts[v].color[2] = tintB;
                    overlayVerts[v].color[3] = 255;
                }
                overlayVertices.insert(overlayVertices.end(), overlayVerts, overlayVerts + 4);
                uint32_t ovIdx[6] = {
                    overlayBase, overlayBase + 1, overlayBase + 2,
                    overlayBase, overlayBase + 2, overlayBase + 3
                };
                overlayIndices.insert(overlayIndices.end(), ovIdx, ovIdx + 6);
            }
        }
    }

}

// ============================================================
// 辅助函数：获取全局坐标的 blockState
// ============================================================
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
    baseVertices.reserve(50000);
    baseIndices.reserve(75000);
    overlayVertices.reserve(8000);
    overlayIndices.reserve(12000);
    waterVertices.reserve(4000);
    waterIndices.reserve(6000);
    float baseX = chunkX * CHUNK_WIDTH;
    float baseY = static_cast<float>(sectionY);
    float baseZ = chunkZ * CHUNK_DEPTH;

    // 缓存特殊纹理层索引（从 TextureAtlas 动态查询）
    auto& atlas = TextureAtlas::getInstance();
    float grassSideLayer = static_cast<float>(atlas.getGrassSideLayer());
    float grassOverlayLayer = static_cast<float>(atlas.getGrassSideOverlayLayer());
    float grassSnowLayer = static_cast<float>(atlas.getGrassBlockSnowLayer());

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

    // 模型缓存：按方块名缓存 getBlockModel 结果，避免重复 string map 查找
    std::unordered_map<std::string, const ResolvedBlockModel*> modelCache;
    auto getModel = [&](const std::string& name) -> const ResolvedBlockModel* {
        auto it = modelCache.find(name);
        if (it != modelCache.end()) return it->second;
        auto* m = atlas.getBlockModel(name);
        modelCache[name] = m;
        return m;
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

                // 跳过空气变种（如 cave_air, void_air）
                if (blockMeta.isAir) continue;

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

                // ===== 模型驱动渲染（优先，适用于所有有模型数据的方块） =====
                // 跳过水：水使用独立的渲染管道（alpha blend + 动画纹理）
                bool isSnowCovered2 = (blockMeta.isGrassBlock && n[TOP] != 0 &&
                                      BlockRegistry::getInstance().getBlockMetadata(n[TOP]).isSnow);
                if (!blockMeta.isWater) {
                    // Blockstate 变体查找（获取朝向对应的模型和旋转）
                    const BlockStateVariant* variant = atlas.getBlockStateVariant(
                        blockMeta.name, blockState, blockMeta.minStateId);
                    const std::string* modelName = variant ? &variant->modelName : &blockMeta.name;
                    int bsRotX = variant ? variant->rotX : 0;
                    int bsRotY = variant ? variant->rotY : 0;

                    const auto* blockModel = getModel(*modelName);
                    if (blockModel && !blockModel->elements.empty()) {
                        generateFromModel(
                            baseVertices, baseIndices,
                            overlayVertices, overlayIndices,
                            *blockModel,
                            posX, posY, posZ,
                            n,
                            blockMeta.isGrassBlock, isSnowCovered2,
                            grassSideLayer, grassOverlayLayer,
                            tintR, tintG, tintB,
                            biomeId,
                            &solidCache,
                            bsRotX, bsRotY);
                        continue;
                    }
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
                        ? grassSnowLayer
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
                        sideTex = grassSideLayer;
                        needsOverlay = true;
                    } else if (isSnowCovered) {
                        sideTex = grassSnowLayer;
                    } else {
                        sideTex = static_cast<float>(tex.side);
                        sr = tintR; sg = tintG; sb = tintB;
                    }

                    addCubicFace(baseVertices, baseIndices, sf,
                                 posX, posY, posZ, blockHeight, sideTex, sr, sg, sb, 255);

                    if (needsOverlay) {
                        addCubicFace(overlayVertices, overlayIndices, sf,
                                     posX, posY, posZ, blockHeight,
                                     grassOverlayLayer,
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

