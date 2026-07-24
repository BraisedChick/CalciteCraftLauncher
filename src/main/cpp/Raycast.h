#pragma once
#include <glm/glm.hpp>
#include "TextureAtlas.h"  // for FaceDir

class ChunkManager;
class EntityManager;

struct RaycastResult {
    bool hit = false;
    int blockX = 0, blockY = 0, blockZ = 0;   // 被击中方块的坐标
    int8_t hitFace = FACE_NONE;                 // 被击中的面（用于放置方块）
    float distance = 0.0f;                      // 击中距离
};

// DDA 体素射线遍历
RaycastResult rayCast(glm::vec3 origin, glm::vec3 direction,
                      float maxDist, const ChunkManager& chunkManager);

// 便捷封装：以摄像机眼睛位置和朝向发射射线，检测方块
RaycastResult rayCastFromCamera(float maxDist, const ChunkManager& chunkManager);

// 返回准星下最近的敌对/可交互实体 ID，若无则返回 -1
int rayCastEntity(float maxDist, const EntityManager& entityManager, int excludeEntityId);
