#include "EntityManager.h"
#include <android/log.h>

#define LOG_TAG "EntityManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

void EntityManager::addEntity(const Entity& entity) {
    std::lock_guard<std::mutex> lock(mutex);
    entities[entity.entityId] = entity;
    // 初始化插值位置
    auto& e = entities[entity.entityId];
    e.prevX = e.x;
    e.prevY = e.y;
    e.prevZ = e.z;
    e.prevYaw = e.yaw;
    e.prevHeadYaw = e.headYaw;
    LOGI("Entity added: id=%d type=%d (%s) at (%.1f, %.1f, %.1f)",
         e.entityId, (int)e.type, e.getTypeName(), e.x, e.y, e.z);
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
    e.yaw = yaw;
    e.pitch = pitch;
}

void EntityManager::rotateEntity(int entityId, float yaw, float pitch) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = entities.find(entityId);
    if (it == entities.end()) return;
    auto& e = it->second;
    e.prevYaw = e.yaw;
    e.yaw = yaw;
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
    e.yaw = yaw;
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

size_t EntityManager::getEntityCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return entities.size();
}

void EntityManager::tick(float partialTick) {
    // 预留：可在此做客户端预测（重力、碰撞等）
    // 目前渲染时直接做 prevX→X 的插值
}