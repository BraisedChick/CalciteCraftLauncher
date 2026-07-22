#include "MeshGenerator.h"
#include "BlockRegistry.h"
#include "TextureAtlas.h"
#include "BiomeColorManager.h"
#include "gui/GameUI.h"
#include <android/log.h>
#include <map>
#include <string>
#include <cmath>

// 添加 GLM（用于模型元素旋转）
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <unordered_set>
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

// 面法线查找表（Face 枚举 → 法线向量）
// TOP=0, BOTTOM=1, RIGHT=2, LEFT=3, FRONT=4, BACK=5
static const float FACE_NORMALS[6][3] = {
    { 0.0f,  1.0f,  0.0f},  // TOP
    { 0.0f, -1.0f,  0.0f},  // BOTTOM
    { 1.0f,  0.0f,  0.0f},  // RIGHT / EAST
    {-1.0f,  0.0f,  0.0f},  // LEFT / WEST
    { 0.0f,  0.0f,  1.0f},  // FRONT / SOUTH
    { 0.0f,  0.0f, -1.0f},  // BACK / NORTH
};

// FaceDir 法线查找表（FaceDir 枚举 → 法线向量）
// DOWN=0, UP=1, NORTH=2, SOUTH=3, WEST=4, EAST=5
static const float FACEDIR_NORMALS[6][3] = {
    { 0.0f, -1.0f,  0.0f},  // DOWN
    { 0.0f,  1.0f,  0.0f},  // UP
    { 0.0f,  0.0f, -1.0f},  // NORTH
    { 0.0f,  0.0f,  1.0f},  // SOUTH
    {-1.0f,  0.0f,  0.0f},  // WEST
    { 1.0f,  0.0f,  0.0f},  // EAST
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
        vert.normal[0] = FACE_NORMALS[face][0];
        vert.normal[1] = FACE_NORMALS[face][1];
        vert.normal[2] = FACE_NORMALS[face][2];
        // uv2 使用默认全亮值 (240, 240)，已在 Vertex 构造中设置
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

// 将 cullface 方向（FaceDir）依次经元素旋转 + blockstate 旋转后，返回变换后的 FaceDir
static int8_t transformCullface(int8_t cullface, const ElementRotation& elemRot, int bsRotX, int bsRotY) {
    // FaceDir: DOWN=0,UP=1,NORTH=2,SOUTH=3,WEST=4,EAST=5
    static const float DIRS[6][3] = {
        { 0, -1,  0},  // DOWN
        { 0,  1,  0},  // UP
        { 0,  0, -1},  // NORTH
        { 0,  0,  1},  // SOUTH
        {-1,  0,  0},  // WEST
        { 1,  0,  0},  // EAST
    };

    float x = DIRS[cullface][0];
    float y = DIRS[cullface][1];
    float z = DIRS[cullface][2];

    // 1. 元素旋转（与 rotateVertex 一致，正角度）
    if (elemRot.angle != 0.0f) {
        float rad = elemRot.angle * (3.14159265f / 180.0f);
        float cosA = cosf(rad), sinA = sinf(rad);
        float nx, ny, nz;
        switch (elemRot.axis) {
            case 0: // X
                ny = y * cosA - z * sinA;
                nz = y * sinA + z * cosA;
                nx = x;
                break;
            case 1: // Y
                nx = x * cosA + z * sinA;
                nz = -x * sinA + z * cosA;
                ny = y;
                break;
            case 2: // Z
                nx = x * cosA - y * sinA;
                ny = x * sinA + y * cosA;
                nz = z;
                break;
            default:
                nx = x; ny = y; nz = z;
        }
        x = nx; y = ny; z = nz;
    }

    // 2. Blockstate 旋转 bsRotX（与顶点代码一致，负角度）
    if (bsRotX != 0) {
        float rad = -bsRotX * (3.14159265f / 180.0f);
        float cosA = cosf(rad), sinA = sinf(rad);
        float ny = y * cosA - z * sinA;
        float nz = y * sinA + z * cosA;
        y = ny; z = nz;
    }

    // 3. Blockstate 旋转 bsRotY（与顶点代码一致，负角度）
    if (bsRotY != 0) {
        float rad = -bsRotY * (3.14159265f / 180.0f);
        float cosA = cosf(rad), sinA = sinf(rad);
        float nx = x * cosA + z * sinA;
        float nz = -x * sinA + z * cosA;
        x = nx; z = nz;
    }

    // 找到最接近的单位轴向
    int8_t best = cullface;
    float bestDot = -999.0f;
    for (int d = 0; d < 6; d++) {
        float dot = x * DIRS[d][0] + y * DIRS[d][1] + z * DIRS[d][2];
        if (dot > bestDot) { bestDot = dot; best = (int8_t)d; }
    }
    return best;
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
    int bsRotX, int bsRotY) {

    // 面剔除检测：直接读取预计算的 isFullBlock + isOpaque（无锁）
    auto isNeighborSolid = [](int32_t state) -> bool {
        if (state == 0) return false;
        const auto& meta = BlockRegistry::getInstance().getBlockMetadata(state);
        return meta.isFullBlock && meta.isOpaque;
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
            // cullface 方向需经元素旋转和 blockstate 旋转变换
            if (face.cullface >= 0 && face.cullface < 6) {
                int8_t actualCullface = transformCullface(face.cullface, elem.rotation, bsRotX, bsRotY);
                int32_t neighbor = neighborStates[FACEDIR_TO_FACE[actualCullface]];
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

            // 用 fv 模板计算每个顶点默认 UV
            float vertUV[4][2];
            for (int v = 0; v < 4; v++) {
                const float* fvp = FV[fvIndex][v];
                vertUV[v][0] = u1 + fvp[3] * (u2 - u1);
                vertUV[v][1] = v1 + fvp[4] * (v2 - v1);
            }
            // UV 旋转：vertUV 数组旋转 (shift = 90/180/270)
            // 所有面从外部看，UV的顺时针旋转在屏幕上都是顺时针，无需根据法线方向取反
            if (face.rotation != 0) {
                float angleRad = face.rotation * (3.14159265f / 180.0f);
                float cosA = cosf(angleRad);
                float sinA = sinf(angleRad);
                for (int v = 0; v < 4; v++) {
                    float u = vertUV[v][0] - 0.5f;
                    float vv = vertUV[v][1] - 0.5f;
                    float nu = u * cosA + vv * sinA;
                    float nv = -u * sinA + vv * cosA;
                    vertUV[v][0] = nu + 0.5f;
                    vertUV[v][1] = nv + 0.5f;
                }
            }
            // FV[0](TOP)模板的 fv[4]=[0,0,1,1] 与其他模板 [1,1,0,0] 相反，
            // 导致 V 方向反了 180°，需翻转补偿
            if (fvIndex == 0) {
                for (int v = 0; v < 4; v++) {
                    vertUV[v][1] = 1.0f - vertUV[v][1];
                }
            }

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
            // tintindex 匹配时优先用调用方预计算的颜色（grass_block/leaves），否则从 BiomeColorManager 采样
            // 非 tintindex 面保持白色（如草方块侧面由 overlay 着色）
            if (face.tintindex == 0) {
                if (tintR != 255 || tintG != 255 || tintB != 255) {
                    cr = tintR; cg = tintG; cb = tintB;
                } else {
                    BiomeColorManager::getInstance().getGrassColor(biomeId, cr, cg, cb);
                }
            } else if (face.tintindex == 1) {
                if (tintR != 255 || tintG != 255 || tintB != 255) {
                    cr = tintR; cg = tintG; cb = tintB;
                } else {
                    BiomeColorManager::getInstance().getFoliageColor(biomeId, cr, cg, cb);
                }
            } else if (needsOverlay) {
                // overlay 面：base 面白色，颜色由 overlay 携带
                cr = 255; cg = 255; cb = 255;
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

                // Blockstate 旋转（整个模型绕方块中心 8,8,8，顺时针）
                if (bsRotX != 0 || bsRotY != 0) {
                    float cx = lx - 8.0f, cy = ly - 8.0f, cz = lz - 8.0f;
                    if (bsRotX != 0) {
                        float rad = -bsRotX * (3.14159265f / 180.0f);
                        float cosA = cosf(rad), sinA = sinf(rad);
                        float ny = cy * cosA - cz * sinA;
                        float nz = cy * sinA + cz * cosA;
                        cy = ny; cz = nz;
                    }
                    if (bsRotY != 0) {
                        float rad = -bsRotY * (3.14159265f / 180.0f);
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

                // UV 坐标：从 vertUV 取当前 vertex 的默认 UV（后面统一旋转）
                faceVerts[v].texCoord[0] = vertUV[v][0];
                faceVerts[v].texCoord[1] = vertUV[v][1];
                faceVerts[v].texIndex = texLayer;
                faceVerts[v].color[0] = cr;
                faceVerts[v].color[1] = cg;
                faceVerts[v].color[2] = cb;
                faceVerts[v].color[3] = 255;
                // 面法线（使用 FaceDir 法线，未旋转——Mojang 着色器会用 ModelViewMat 变换）
                faceVerts[v].normal[0] = FACEDIR_NORMALS[dir][0];
                faceVerts[v].normal[1] = FACEDIR_NORMALS[dir][1];
                faceVerts[v].normal[2] = FACEDIR_NORMALS[dir][2];
                // uv2 默认全亮 (240, 240)
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

    // 线程局部的 scratch vectors，跨 section 复用避免反复分配
    std::vector<Vertex> baseVertices, overlayVertices, waterVertices;
    std::vector<uint32_t> baseIndices, overlayIndices, waterIndices;

    for (size_t sectionIdx = 0; sectionIdx < chunk.sections.size(); ++sectionIdx) {
        const auto& section = chunk.sections[sectionIdx];
        if (!section || section->isEmpty) continue;
        int sectionY = section->y;
        auto meshOut = generateSectionMesh(*section, chunk.pos.x, sectionY, chunk.pos.z, nullptr,
                                           baseVertices, baseIndices,
                                           overlayVertices, overlayIndices,
                                           waterVertices, waterIndices);
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
                                                                    const ChunkManager* chunkManager,
                                                                    std::vector<Vertex>& baseVertices,
                                                                    std::vector<uint32_t>& baseIndices,
                                                                    std::vector<Vertex>& overlayVertices,
                                                                    std::vector<uint32_t>& overlayIndices,
                                                                    std::vector<Vertex>& waterVertices,
                                                                    std::vector<uint32_t>& waterIndices) {
    // 复用外部 scratch vectors（clear 保留 capacity，避免重新分配）
    baseVertices.clear();
    baseIndices.clear();
    overlayVertices.clear();
    overlayIndices.clear();
    waterVertices.clear();
    waterIndices.clear();
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

    // 面剔除检测：几何完整且不透明的方块才会遮挡相邻面
    auto isSolid = [](int32_t state) -> bool {
        if (state == 0) return false;
        const auto& meta = BlockRegistry::getInstance().getBlockMetadata(state);
        return meta.isFullBlock && meta.isOpaque;
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

// 阶段计时
    int countModel = 0, countWater = 0, countCubic = 0;

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
                    // 白桦树叶固定 #80A755，云杉树叶固定 #619961（Java 版硬编码，不受生物群系影响）
                    if (blockMeta.name == "birch_leaves") {
                        tintR = 0x80; tintG = 0xA7; tintB = 0x55;
                    } else if (blockMeta.name == "spruce_leaves") {
                        tintR = 0x61; tintG = 0x99; tintB = 0x61;
                    } else {
                        BiomeColorManager::getInstance().getFoliageColor(biomeId, tintR, tintG, tintB);
                    }
                } else if (blockMeta.name == "redstone_wire") {
                    // 红石粉颜色：根据 power 值从暗红到亮红
                    // power 0: #4B0000 (暗红) → power 15: #FF0000 (亮红)
                    // 从 blockState 提取 power 值
                    int power = 0;
                    const auto* blockInfo = registry.getBlockInfo(blockState);
                    if (blockInfo && !blockInfo->stateProperties.empty()) {
                        // 计算 power 属性在 state ID 中的位置
                        int offset = blockState - blockInfo->minStateId;
                        // redstone_wire 属性顺序: east(3), north(3), power(16), south(3), west(3)
                        // power 在位置 2，stride = 3*3 = 9
                        power = (offset / 9) % 16;
                    }
                    // 颜色插值：power 0 → #4B0000, power 15 → #FF0000
                    float t = power / 15.0f;
                    tintR = (uint8_t)(0x4B + (0xFF - 0x4B) * t);
                    tintG = 0;
                    tintB = 0;
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

                // ===== 模型驱动渲染（适用于所有有模型数据的方块） =====
                // 跳过水：水使用独立的渲染管道（alpha blend + 动画纹理）
                bool isSnowCovered2 = (blockMeta.isGrassBlock && n[TOP] != 0 &&
                                      BlockRegistry::getInstance().getBlockMetadata(n[TOP]).isSnow);

                if (!blockMeta.isWater) {
                    // Blockstate 变体查找
                    const BlockStateVariant* variant = atlas.getBlockStateVariant(
                            blockMeta.name, blockState, blockMeta.minStateId);

                    if (variant && !variant->models.empty()) {
                        bool renderedAny = false;
                        // 用模型指针去重，而非模型名字符串
                        std::unordered_set<const ResolvedBlockModel*> renderedModels;
                        for (const auto& modelEntry : variant->models) {
                            const auto* blockModel = getModel(modelEntry.modelName);
                            if (blockModel && !blockModel->elements.empty()) {
                                // 如果这个模型对象已经渲染过，跳过
                                if (renderedModels.find(blockModel) != renderedModels.end()) {
                                    continue;
                                }
                                renderedModels.insert(blockModel);

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
                                        modelEntry.rotX, modelEntry.rotY);
                                countModel++;
                                renderedAny = true;
                            }
                        }
                        if (renderedAny) continue;
                    }

                    // 如果没有变体或变体没有有效模型，回退到默认模型
                    const auto* blockModel = getModel(blockMeta.name);
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
                                0, 0);
                        countModel++;
                        continue;
                    }
                }

                // ===== 水 =====
                if (blockMeta.isWater) {
                    countWater++;
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
                countCubic++;
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

    // ===== 光照：基于 blockStates 位置查 skyLight/blockLight，计算 UV2 =====

    // 跨区块光照查询：世界坐标 → 光照值
    auto getLightAtWorld = [&](int worldX, int worldY, int worldZ,
                               uint8_t& outSky, uint8_t& outBlock) {
        int cx = (worldX >> 4);
        int cz = (worldZ >> 4);
        int lx = worldX & 15;
        int lz = worldZ & 15;

        // 优先在当前 section 查找（快速路径）
        if (cx == chunkX && cz == chunkZ &&
            worldY >= sectionY && worldY < sectionY + 16) {
            outSky = section.getSkyLight(lx, worldY - sectionY, lz);
            outBlock = section.getBlockLight(lx, worldY - sectionY, lz);
            return;
        }

        // 跨区块查询
        if (!chunkManager) {
            outSky = 15; outBlock = 0; return;
        }
        auto chunk = chunkManager->getChunk(cx, cz);
        if (!chunk || !chunk->isLoaded) {
            outSky = 15; outBlock = 0; return;
        }
        for (const auto& s : chunk->sections) {
            if (!s) continue;
            if (worldY >= s->y && worldY < s->y + 16) {
                outSky = s->getSkyLight(lx, worldY - s->y, lz);
                outBlock = s->getBlockLight(lx, worldY - s->y, lz);
                return;
            }
        }
        outSky = 15; outBlock = 0;
    };

    // 平滑光照辅助函数：对顶点周围 4 个方块做平均
    auto getSmoothLight = [&](float wx, float wy, float wz,
                               float nx, float ny, float nz,
                               uint8_t& outSky, uint8_t& outBlock) {
        int fnx = (nx > 0.5f) ? 1 : ((nx < -0.5f) ? -1 : 0);
        int fny = (ny > 0.5f) ? 1 : ((ny < -0.5f) ? -1 : 0);
        int fnz = (nz > 0.5f) ? 1 : ((nz < -0.5f) ? -1 : 0);

        int vx = (int)floorf(wx);
        int vy = (int)floorf(wy);
        int vz = (int)floorf(wz);

        int sumSky = 0, sumBlock = 0, count = 0;
        auto smpl = [&](int sx, int sy, int sz) {
            uint8_t sk, bk;
            getLightAtWorld(sx, sy, sz, sk, bk);
            sumSky += sk;
            sumBlock += bk;
            count++;
        };

        // 根据面法线方向，采样顶点周围的 4 个方块（采样面法线指向的空气侧）
        // 偏移量：正方向面=max(fn,0)=1，负方向面=max(fn,0)=0
        int ox = (fnx > 0) ? 1 : 0;
        int oy = (fny > 0) ? 1 : 0;
        int oz = (fnz > 0) ? 1 : 0;

        if (fny != 0) {  // Y 面（TOP/BOTTOM）：在 XZ 平面采样
            for (int dx = 0; dx <= 1; dx++)
                for (int dz = 0; dz <= 1; dz++)
                    smpl(vx - 1 + dx, vy - 1 + oy, vz - 1 + dz);
        } else if (fnx != 0) {  // X 面（EAST/WEST）：在 YZ 平面采样
            for (int dy = 0; dy <= 1; dy++)
                for (int dz = 0; dz <= 1; dz++)
                    smpl(vx - 1 + ox, vy - 1 + dy, vz - 1 + dz);
        } else {  // Z 面（SOUTH/NORTH）：在 XY 平面采样
            for (int dx = 0; dx <= 1; dx++)
                for (int dy = 0; dy <= 1; dy++)
                    smpl(vx - 1 + dx, vy - 1 + dy, vz - 1 + oz);
        }

        if (count > 0) {
            outSky = (uint8_t)((sumSky + count/2) / count);
            outBlock = (uint8_t)((sumBlock + count/2) / count);
        } else {
            outSky = 15; outBlock = 0;
        }
    };

    bool smoothLighting = GameUI::getInstance().isSmoothLightingEnabled();
    {
        auto applyLight = [&](std::vector<Vertex>& verts) {
            for (auto& vert : verts) {
                uint8_t sky = 15, block = 0;
                if (smoothLighting) {
                    getSmoothLight(vert.pos[0], vert.pos[1], vert.pos[2],
                                   vert.normal[0], vert.normal[1], vert.normal[2],
                                   sky, block);
                } else {
                    int wx = (int)floorf(vert.pos[0]);
                    int wy = (int)floorf(vert.pos[1]);
                    int wz = (int)floorf(vert.pos[2]);
                    int lx = wx & 15;
                    int ly = wy - sectionY;
                    int lz = wz & 15;
                    if (ly >= 0 && ly < 16) {
                        sky = section.getSkyLight(lx, ly, lz);
                        block = section.getBlockLight(lx, ly, lz);
                    } else {
                        // 跨 section 边界：查询相邻 section 的光照
                        getLightAtWorld(wx, wy, wz, sky, block);
                    }
                }
                vert.uv2[0] = block * 16.0f + 8.0f;
                vert.uv2[1] = sky * 16.0f + 8.0f;
            }
        };
    applyLight(baseVertices);
    applyLight(overlayVertices);
    applyLight(waterVertices);
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
