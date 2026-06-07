#include "Raycast.h"
#include "ChunkManager.h"
#include "BlockRegistry.h"
#include <cmath>
#include <algorithm>

RaycastResult rayCast(glm::vec3 origin, glm::vec3 direction,
                      float maxDist, const ChunkManager& chunkManager) {
    RaycastResult result;

    // 方向归一化
    float dirLen = glm::length(direction);
    if (dirLen < 1e-6f) return result;
    glm::vec3 dir = direction / dirLen;

    // 起点所在的方块坐标
    int voxelX = (int)std::floor(origin.x);
    int voxelY = (int)std::floor(origin.y);
    int voxelZ = (int)std::floor(origin.z);

    // DDA 步进方向（+1 或 -1）
    int stepX = (dir.x > 0.0f) ? 1 : (dir.x < 0.0f ? -1 : 0);
    int stepY = (dir.y > 0.0f) ? 1 : (dir.y < 0.0f ? -1 : 0);
    int stepZ = (dir.z > 0.0f) ? 1 : (dir.z < 0.0f ? -1 : 0);

    // tMax：沿射线到下一个体素边界所需距离
    // tDelta：穿过一个体素所需距离
    float tMaxX, tMaxY, tMaxZ;
    float tDeltaX, tDeltaY, tDeltaZ;

    if (dir.x != 0.0f) {
        float boundaryX = (stepX > 0) ? (voxelX + 1.0f) : (float)voxelX;
        tMaxX = (boundaryX - origin.x) / dir.x;
        tDeltaX = 1.0f / std::abs(dir.x);
    } else {
        tMaxX = INFINITY;
        tDeltaX = INFINITY;
    }

    if (dir.y != 0.0f) {
        float boundaryY = (stepY > 0) ? (voxelY + 1.0f) : (float)voxelY;
        tMaxY = (boundaryY - origin.y) / dir.y;
        tDeltaY = 1.0f / std::abs(dir.y);
    } else {
        tMaxY = INFINITY;
        tDeltaY = INFINITY;
    }

    if (dir.z != 0.0f) {
        float boundaryZ = (stepZ > 0) ? (voxelZ + 1.0f) : (float)voxelZ;
        tMaxZ = (boundaryZ - origin.z) / dir.z;
        tDeltaZ = 1.0f / std::abs(dir.z);
    } else {
        tMaxZ = INFINITY;
        tDeltaZ = INFINITY;
    }

    // 最大步数限制（防止死循环）
    int maxSteps = 200;

    for (int i = 0; i < maxSteps; i++) {
        // 确定步进轴（三个 tMax 中最小的），同时拿到到该边界的距离
        int axis = 0;
        float tMin = tMaxX;
        if (tMaxY < tMin) { tMin = tMaxY; axis = 1; }
        if (tMaxZ < tMin) { tMin = tMaxZ; axis = 2; }

        // 超出最远距离
        if (tMin > maxDist) break;

        // 沿最小 tMax 轴步进到下一个体素
        if (axis == 0) {
            voxelX += stepX;
            tMaxX += tDeltaX;
        } else if (axis == 1) {
            voxelY += stepY;
            tMaxY += tDeltaY;
        } else {
            voxelZ += stepZ;
            tMaxZ += tDeltaZ;
        }

        // 检查步进后的新方块
        auto chunk = chunkManager.getChunk(voxelX >> 4, voxelZ >> 4);
        if (chunk) {
            int localX = voxelX & 15;
            int localZ = voxelZ & 15;
            uint32_t state = chunk->getBlockState(localX, voxelY, localZ);
            if (state != 0) {
                const auto& meta = BlockRegistry::getInstance().getBlockMetadata((int32_t)state);
                if (!meta.isAir) {
                    result.hit = true;
                    result.blockX = voxelX;
                    result.blockY = voxelY;
                    result.blockZ = voxelZ;
                    result.distance = tMin;
                    // 命中面由步进轴和方向决定
                    if (axis == 0) {
                        result.hitFace = (stepX > 0) ? FACE_WEST : FACE_EAST;
                    } else if (axis == 1) {
                        result.hitFace = (stepY > 0) ? FACE_DOWN : FACE_UP;
                    } else {
                        result.hitFace = (stepZ > 0) ? FACE_NORTH : FACE_SOUTH;
                    }
                    return result;
                }
            }
        }
    }

    return result;
}
