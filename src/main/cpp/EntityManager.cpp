#include "EntityManager.h"
#include <android/log.h>

#define LOG_TAG "EntityManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

void EntityManager::addEntity(const Entity& entity) {
    std::lock_guard<std::mutex> lock(mutex);
    entities[entity.entityId] = entity;
    auto& e = entities[entity.entityId];
    e.prevX = e.x;
    e.prevY = e.y;
    e.prevZ = e.z;
    e.prevYaw = e.yaw;
    e.prevHeadYaw = e.headYaw;
    e.targetHeadYaw = e.headYaw;
}

void EntityManager::removeEntity(int entityId) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it != entities.end()) {
        LOGI("Entity removed: id=%d", entityId);
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
    e.prevX = e.x; e.prevY = e.y; e.prevZ = e.z;
    e.prevYaw = e.yaw;
    e.x += dx / 4096.0;
    e.y += dy / 4096.0;
    e.z += dz / 4096.0;
    e.pitch = pitch;
}

void EntityManager::rotateEntity(int entityId, float yaw, float pitch) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return;
    auto& e = it->second;
    e.prevYaw = e.yaw;
    e.pitch = pitch;
}

void EntityManager::teleportEntity(int entityId, double x, double y, double z, float yaw, float pitch) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return;
    auto& e = it->second;
    e.prevX = e.x; e.prevY = e.y; e.prevZ = e.z;
    e.prevYaw = e.yaw;
    e.x = x; e.y = y; e.z = z;
    e.pitch = pitch;
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
    e.targetHeadYaw = headYaw;
}

size_t EntityManager::getEntityCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return entities.size();
}

void EntityManager::tick(float partialTick) {
    std::lock_guard<std::mutex> lock(mutex);
    for (auto& [id, e] : entities) {
        // 非玩家实体直接使用目标值（无限制）
        if (e.type != EntityType::PLAYER) {
            e.yaw = e.targetHeadYaw;
            e.headYaw = e.targetHeadYaw;
            e.prevYaw = e.yaw;
            e.prevHeadYaw = e.headYaw;
            continue;
        }

        // ---- 玩家逻辑 ----
        // 1. 计算移动量，决定身体基础目标方向 baseYaw
        float dx = (float)(e.x - e.prevX);
        float dz = (float)(e.z - e.prevZ);
        float distSq = dx * dx + dz * dz;
        e.prevX = e.x; e.prevY = e.y; e.prevZ = e.z;

        float baseYaw = e.yaw;
        if (distSq > 0.0025000002f) {
            float f4 = atan2f(dz, dx) * 180.0f / M_PI - 90.0f;
            float yawDiff = e.targetHeadYaw - f4;
            yawDiff = fmodf(yawDiff, 360.0f);
            if (yawDiff > 180.0f) yawDiff -= 360.0f;
            if (yawDiff < -180.0f) yawDiff += 360.0f;
            if (fabsf(yawDiff) > 95.0f && fabsf(yawDiff) < 265.0f)
                baseYaw = f4 - 180.0f;
            else
                baseYaw = f4;
        }

        // 2. 身体延迟跟随（每 tick 向 baseYaw 靠近 30%）
        float bodyDiff = baseYaw - e.yaw;
        bodyDiff = fmodf(bodyDiff, 360.0f);
        if (bodyDiff > 180.0f) bodyDiff -= 360.0f;
        if (bodyDiff < -180.0f) bodyDiff += 360.0f;
        e.yaw += bodyDiff * 0.3f;

        // 3. 头部偏移限制（玩家专用：最大 45 度）
        float headOffset = e.targetHeadYaw - e.yaw;
        headOffset = fmodf(headOffset, 360.0f);
        if (headOffset > 180.0f) headOffset -= 360.0f;
        if (headOffset < -180.0f) headOffset += 360.0f;
        const float MAX_HEAD_ANGLE_PLAYER = 45.0f; // 玩家最大偏转 45°
        if (headOffset > MAX_HEAD_ANGLE_PLAYER) {
            e.yaw = e.targetHeadYaw - MAX_HEAD_ANGLE_PLAYER;
        } else if (headOffset < -MAX_HEAD_ANGLE_PLAYER) {
            e.yaw = e.targetHeadYaw + MAX_HEAD_ANGLE_PLAYER;
        }

        // 头部最终角度直接等于目标值（因为身体已被调整）
        e.headYaw = e.targetHeadYaw;

        // 更新 prev 值（用于渲染插值）
        e.prevYaw = e.yaw;
        e.prevHeadYaw = e.headYaw;
    }
}