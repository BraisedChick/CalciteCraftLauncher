#pragma once
#include <glm/glm.hpp>
#include "TextureAtlas.h"  // for FaceDir

class ChunkManager;

struct RaycastResult {
    bool hit = false;
    int blockX = 0, blockY = 0, blockZ = 0;   // 被击中方块的坐标
    int8_t hitFace = FACE_NONE;                 // 被击中的面（用于放置方块）
    float distance = 0.0f;                      // 击中距离
};

// DDA 体素射线遍历
RaycastResult rayCast(glm::vec3 origin, glm::vec3 direction,
                      float maxDist, const ChunkManager& chunkManager);
