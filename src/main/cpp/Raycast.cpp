#include "Raycast.h"
#include "ChunkManager.h"
#include "BlockRegistry.h"
#include "Collision.h"
#include <cmath>
#include <algorithm>

// 射线与 AABB 相交测试（返回进入和离开距离，不相交则返回 false）
static bool rayIntersectAABB(
    const glm::vec3& origin, const glm::vec3& invDir,
    const AABB& box, float& tEnter, float& tExit) {

    float t1 = (box.minX - origin.x) * invDir.x;
    float t2 = (box.maxX - origin.x) * invDir.x;
    float t3 = (box.minY - origin.y) * invDir.y;
    float t4 = (box.maxY - origin.y) * invDir.y;
    float t5 = (box.minZ - origin.z) * invDir.z;
    float t6 = (box.maxZ - origin.z) * invDir.z;

    tEnter = std::max({std::min(t1, t2), std::min(t3, t4), std::min(t5, t6)});
    tExit  = std::min({std::max(t1, t2), std::max(t3, t4), std::max(t5, t6)});

    return tExit >= std::max(tEnter, 0.0f);
}

// 根据击中点确定面方向
static int8_t determineHitFace(const glm::vec3& origin, const glm::vec3& dir,
                                const AABB& box, float t) {
    glm::vec3 hitPoint = origin + dir * t;

    // 计算击中点相对于 AABB 中心的偏移
    float cx = (box.minX + box.maxX) * 0.5f;
    float cy = (box.minY + box.maxY) * 0.5f;
    float cz = (box.minZ + box.maxZ) * 0.5f;

    float dx = hitPoint.x - cx;
    float dy = hitPoint.y - cy;
    float dz = hitPoint.z - cz;

    float halfX = (box.maxX - box.minX) * 0.5f;
    float halfY = (box.maxY - box.minY) * 0.5f;
    float halfZ = (box.maxZ - box.minZ) * 0.5f;

    // 归一化偏移，找最大分量
    float ax = (halfX > 0.001f) ? std::abs(dx / halfX) : 0.0f;
    float ay = (halfY > 0.001f) ? std::abs(dy / halfY) : 0.0f;
    float az = (halfZ > 0.001f) ? std::abs(dz / halfZ) : 0.0f;

    if (ax >= ay && ax >= az) {
        return (dx > 0) ? FACE_EAST : FACE_WEST;
    } else if (ay >= ax && ay >= az) {
        return (dy > 0) ? FACE_UP : FACE_DOWN;
    } else {
        return (dz > 0) ? FACE_SOUTH : FACE_NORTH;
    }
}

RaycastResult rayCast(glm::vec3 origin, glm::vec3 direction,
                      float maxDist, const ChunkManager& chunkManager) {
    RaycastResult result;

    // 方向归一化
    float dirLen = glm::length(direction);
    if (dirLen < 1e-6f) return result;
    glm::vec3 dir = direction / dirLen;

    // 预计算逆方向（用于 AABB 相交测试，避免除法）
    glm::vec3 invDir(
        (std::abs(dir.x) > 1e-8f) ? 1.0f / dir.x : 1e8f,
        (std::abs(dir.y) > 1e-8f) ? 1.0f / dir.y : 1e8f,
        (std::abs(dir.z) > 1e-8f) ? 1.0f / dir.z : 1e8f);

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
    float closestDist = maxDist + 1.0f;

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
        if (!chunk) continue;

        int localX = voxelX & 15;
        int localZ = voxelZ & 15;
        uint32_t state = chunk->getBlockState(localX, voxelY, localZ);
        if (state == 0) continue;

        const auto& meta = BlockRegistry::getInstance().getBlockMetadata((int32_t)state);
        if (meta.isAir) continue;

        // 获取方块的碰撞箱
        std::vector<AABB> boxes;
        if (meta.isPlant || meta.isNoCollision) {
            // 植物和无碰撞方块：使用完整方块 AABB 进行射线判定（可以被点中/破坏）
            boxes.push_back(AABB((float)voxelX, (float)voxelY, (float)voxelZ,
                                 (float)(voxelX + 1), (float)(voxelY + 1), (float)(voxelZ + 1)));
        } else {
            // 普通方块：使用精确碰撞箱
            boxes = Collision::getInstance().getBlockAABBs(voxelX, voxelY, voxelZ);
            if (boxes.empty()) {
                // 无碰撞箱数据，回退到完整方块
                boxes.push_back(AABB((float)voxelX, (float)voxelY, (float)voxelZ,
                                     (float)(voxelX + 1), (float)(voxelY + 1), (float)(voxelZ + 1)));
            }
        }

        for (const auto& box : boxes) {
            float tEnter, tExit;
            if (rayIntersectAABB(origin, invDir, box, tEnter, tExit) && tEnter >= 0.0f && tEnter < closestDist) {
                closestDist = tEnter;
                result.hit = true;
                result.blockX = voxelX;
                result.blockY = voxelY;
                result.blockZ = voxelZ;
                result.distance = tEnter;
                result.hitFace = determineHitFace(origin, dir, box, tEnter);
            }
        }

        // 如果已经找到交点且当前体素已超出交点距离，可以提前退出
        if (result.hit && tMin > closestDist) break;
    }

    return result;
}
