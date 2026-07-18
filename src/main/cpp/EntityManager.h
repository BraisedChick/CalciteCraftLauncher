#pragma once

#include "Entity.h"
#include <unordered_map>
#include <mutex>
#include <vector>

// 实体管理器（线程安全）
// 网络线程写入，渲染线程读取
class EntityManager {
public:
    static EntityManager& getInstance();

    // 添加/更新实体（网络线程调用）
    void addEntity(const Entity& entity);
    void removeEntity(int entityId);
    void removeAllEntities();

    // 更新实体位置（相对移动，MC 用 delta * 1/4096 blocks）
    void moveEntity(int entityId, short dx, short dy, short dz);
    void moveEntityRot(int entityId, short dx, short dy, short dz, float yaw, float pitch);
    void rotateEntity(int entityId, float yaw, float pitch);

    // 传送（绝对位置）
    void teleportEntity(int entityId, double x, double y, double z, float yaw, float pitch);

    // 设置实体速度
    void setEntityMotion(int entityId, short vx, short vy, short vz);

    // 读取并清零实体速度（用于 Collision 消费一次性的击退冲量）
    void consumeEntityMotion(int entityId, double& vx, double& vy, double& vz);

    // 渲染线程：获取所有实体快照（线程安全拷贝）
    std::vector<Entity> getAllEntities() const;

    // 获取单个实体（线程安全）
    bool getEntity(int entityId, Entity& out) const;

    // 获取实体数量
    size_t getEntityCount() const;

    // 每帧更新（渲染线程调用）- 更新插值位置
    void tick(float partialTick);

private:
    EntityManager() = default;

    std::unordered_map<int, Entity> entities;
    mutable std::mutex mutex;
};
