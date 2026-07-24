#pragma once
#include <glm/glm.hpp>
#include "TextureAtlas.h"  // for FaceDir

class ChunkManager;
class EntityManager;
class GameEngine;

struct RaycastResult {
    bool hit = false;
    int blockX = 0, blockY = 0, blockZ = 0;   // 被击中方块的坐标
    int8_t hitFace = FACE_NONE;                 // 被击中的面（用于放置方块）
    float distance = 0.0f;                      // 击中距离
};

// 摄像机射线的综合结果：优先实体，其次方块
struct RaycastTarget {
    int entityId = -1;        // 命中的实体 ID，-1 表示未命中实体
    RaycastResult block;      // 方块射线结果
    bool hasEntity() const { return entityId >= 0; }
    bool hasBlock() const { return block.hit; }
};

// 射线检测器：通过依赖注入持有 GameEngine，运行时惰性获取
// chunkManager / entityManager / collision / BlockRegistry，不再依赖任何单例
class Raycast {
public:
    explicit Raycast(GameEngine* engine);
    ~Raycast() = default;

    // 禁止拷贝
    Raycast(const Raycast&) = delete;
    Raycast& operator=(const Raycast&) = delete;

    // 综合查询：以摄像机视角检测实体（优先）与方块
    // 实体 reach 依据游戏模式内部决定，maxDist 用于方块射线
    RaycastTarget getTargetFromCamera(float maxDist, int excludeEntityId);

    // 便捷封装：以摄像机眼睛位置和朝向发射射线，检测方块
    RaycastResult rayCastFromCamera(float maxDist);

    // DDA 体素射线遍历（底层原始射线）
    RaycastResult rayCast(glm::vec3 origin, glm::vec3 direction, float maxDist);

    // 返回准星下最近的敌对/可交互实体 ID，若无则返回 -1
    int rayCastEntity(float maxDist, int excludeEntityId);

private:
    GameEngine* m_engine = nullptr;

    // 计算摄像机眼睛位置和朝向向量
    void getCameraEyeRay(glm::vec3& eyePos, glm::vec3& dir) const;
};
