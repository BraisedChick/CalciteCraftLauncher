#include "EntityManager.h"
#include <android/log.h>

#define LOG_TAG "EntityManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

void EntityManager::addEntity(const Entity& entity) {
    std::lock_guard<std::mutex> lock(mutex);
    entities[entity.entityId] = entity;
    auto& e = entities[entity.entityId];
    // 初始化 prevX/Z 用于移动检测
    e.prevX = e.x; e.prevY = e.y; e.prevZ = e.z;
    // 身体默认等于逻辑朝向
    e.bodyYaw = e.yaw;
}

void EntityManager::removeEntity(int entityId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it != entities.end()) {
        entities.erase(it);
    }
}

void EntityManager::removeAllEntities() {
    std::lock_guard<std::mutex> lock(mutex);
    LOGI("All entities removed (%zu)", entities.size());
    entities.clear();
}

void EntityManager::moveEntity(int entityId, short dx, short dy, short dz) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return;
    auto& e = it->second;
    e.prevX = e.x; e.prevY = e.y; e.prevZ = e.z;
    // MC 相对移动：delta / 4096 blocks
    e.x += dx / 4096.0;
    e.y += dy / 4096.0;
    e.z += dz / 4096.0;
}

void EntityManager::moveEntityRot(int entityId, short dx, short dy, short dz, float yaw, float pitch) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return;
    auto& e = it->second;
    // 更新位置
    e.x += dx / 4096.0;
    e.y += dy / 4096.0;
    e.z += dz / 4096.0;
    // 更新逻辑朝向
    e.yaw = yaw;
    // 非玩家直接同步身体
    if (e.type != EntityType::PLAYER) {
        e.bodyYaw = yaw;
    }
    e.pitch = pitch;
}

void EntityManager::rotateEntity(int entityId, float yaw, float pitch) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return;
    auto& e = it->second;
    e.yaw = yaw;
    if (e.type != EntityType::PLAYER) {
        e.bodyYaw = yaw;
    }
    e.pitch = pitch;
}

void EntityManager::teleportEntity(int entityId, double x, double y, double z, float yaw, float pitch) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return;
    auto& e = it->second;
    e.x = x; e.y = y; e.z = z;
    e.yaw = yaw;
    if (e.type != EntityType::PLAYER) {
        e.bodyYaw = yaw;
    }
    e.pitch = pitch;
    // 重置 prevX/Z 防止瞬间移动被误判为移动量
    e.prevX = x; e.prevY = y; e.prevZ = z;
}

void EntityManager::setEntityMotion(int entityId, short vx, short vy, short vz) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return;
    auto& e = it->second;
    // MC velocity: short / 8000 blocks per tick
    e.vx = vx / 8000.0;
    e.vy = vy / 8000.0;
    e.vz = vz / 8000.0;
}

void EntityManager::consumeEntityMotion(int entityId, double& vx, double& vy, double& vz) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return;
    vx = it->second.vx; it->second.vx = 0;
    vy = it->second.vy; it->second.vy = 0;
    vz = it->second.vz; it->second.vz = 0;
}

std::vector<Entity> EntityManager::getAllEntities() const {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Entity> result;
    result.reserve(entities.size());
    for (const auto& [id, entity] : entities) {
        if (!entity.removed) {
            result.push_back(entity);
        }
    }
    return result;
}

bool EntityManager::getEntity(int entityId, Entity& out) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return false;
    out = it->second;
    return true;
}

void EntityManager::setHeadYaw(int entityId, float headYaw) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return;
    auto& e = it->second;
    e.headYaw = headYaw;
}

size_t EntityManager::getEntityCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return entities.size();
}

void EntityManager::tick(float partialTick) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& [id, e] : entities) {
        // 非玩家实体暂时不处理
        if (e.type == EntityType::PLAYER) {

            // ---- 玩家逻辑 ----
            // 1. 计算移动量，决定身体基础目标方向 baseYaw
            float dx = (float)(e.x - e.prevX);
            float dz = (float)(e.z - e.prevZ);
            float distSq = dx * dx + dz * dz;
            e.prevX = e.x; e.prevY = e.y; e.prevZ = e.z;

            float baseYaw = e.bodyYaw;
            if (distSq > 0.0025000002f) {
                float f4 = atan2f(dz, dx) * 180.0f / M_PI - 90.0f;
                float yawDiff = e.yaw - f4;
                yawDiff = fmodf(yawDiff, 360.0f);
                if (yawDiff > 180.0f) yawDiff -= 360.0f;
                if (yawDiff < -180.0f) yawDiff += 360.0f;
                if (fabsf(yawDiff) > 95.0f && fabsf(yawDiff) < 265.0f)
                    baseYaw = f4 - 180.0f;
                else
                    baseYaw = f4;
            }

            // 2. 身体延迟跟随
            float bodyDiff = baseYaw - e.bodyYaw;
            bodyDiff = fmodf(bodyDiff, 360.0f);
            if (bodyDiff > 180.0f) bodyDiff -= 360.0f;
            if (bodyDiff < -180.0f) bodyDiff += 360.0f;
            e.bodyYaw += bodyDiff * 0.3f;

            // 3. 头部偏移限制（45°）
            float headOffset = e.headYaw - e.bodyYaw;
            headOffset = fmodf(headOffset, 360.0f);
            if (headOffset > 180.0f) headOffset -= 360.0f;
            if (headOffset < -180.0f) headOffset += 360.0f;
            const float MAX_HEAD_ANGLE_PLAYER = 45.0f;
            if (headOffset > MAX_HEAD_ANGLE_PLAYER) {
                e.bodyYaw = e.headYaw - MAX_HEAD_ANGLE_PLAYER;
            } else if (headOffset < -MAX_HEAD_ANGLE_PLAYER) {
                e.bodyYaw = e.headYaw + MAX_HEAD_ANGLE_PLAYER;
            }
        }
    }
}