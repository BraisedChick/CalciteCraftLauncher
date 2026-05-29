#include "Collision.h"
#include "ChunkManager.h"
#include "BlockRegistry.h"
#include <glm/gtc/matrix_transform.hpp>
#include <android/log.h>
#include <cmath>

#define LOG_TAG "Collision"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define KEY_W 0
#define KEY_S 1
#define KEY_A 2
#define KEY_D 3
#define KEY_UP 4
#define KEY_DOWN 5

Collision::Collision() {
    LOGI("Collision initialized at (%.2f, %.2f, %.2f)",
         position.x, position.y, position.z);
}

void Collision::setChunkManager(ChunkManager* mgr) {
    std::lock_guard<std::mutex> lock(mutex);
    chunkManager = mgr;
}

bool Collision::hasChunkManager() const {
    std::lock_guard<std::mutex> lock(mutex);
    return chunkManager != nullptr;
}

void Collision::setKeyState(int key, bool pressed) {
    std::lock_guard<std::mutex> lock(mutex);
    switch (key) {
        case KEY_W: keyW = pressed; break;
        case KEY_S: keyS = pressed; break;
        case KEY_A: keyA = pressed; break;
        case KEY_D: keyD = pressed; break;
        case KEY_UP: jumpPressed = pressed; break;
        case KEY_DOWN: keyDown = pressed; break;
    }
}

void Collision::setJoystickInput(float dx, float dy) {
    std::lock_guard<std::mutex> lock(mutex);
    joystickDX = dx;
    joystickDY = dy;
}

// ===== 物理更新 =====

void Collision::update(float deltaTime, float camPitch, float camYaw) {
    std::lock_guard<std::mutex> lock(mutex);

    // 从 CameraController 同步视角方向
    pitch = camPitch;
    yaw = camYaw;

    // 如果玩家所在的区块未加载，暂停物理更新（防掉虚空）
    if (chunkManager) {
        int cx = (int)floorf(position.x) >> 4;
        int cz = (int)floorf(position.z) >> 4;
        auto chunk = chunkManager->getChunk(cx, cz);
        if (!chunk || !chunk->isLoaded) {
            accumulatedTime = 0.0f;
            return;
        }
    }

    // 固定时间步长累积器（20 ticks/s）
    accumulatedTime += deltaTime;
    if (accumulatedTime >= TICK_DURATION) {
        prevPosition = position;
    }
    while (accumulatedTime >= TICK_DURATION) {
        tick();
        accumulatedTime -= TICK_DURATION;
    }
}

void Collision::tick() {
    if (!chunkManager) return;

    // ---- 计算水平移动方向 ----
    float cosYaw = cosf(yaw);
    float sinYaw = sinf(yaw);
    float cosPitch = cosf(pitch);
    float sinPitch = sinf(pitch);

    glm::vec3 front;
    front.x = -sinYaw * cosPitch;
    front.y = -sinPitch;
    front.z = cosYaw * cosPitch;
    front = glm::normalize(front);

    glm::vec3 horizontalFront(front.x, 0.0f, front.z);
    if (glm::length(horizontalFront) > 1e-6f) {
        horizontalFront = glm::normalize(horizontalFront);
    }

    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(front, worldUp));

    glm::vec3 moveDir(0.0f, 0.0f, 0.0f);

    // 键盘
    if (keyW) moveDir += horizontalFront;
    if (keyS) moveDir -= horizontalFront;
    if (keyA) moveDir -= right;
    if (keyD) moveDir += right;

    // 摇杆
    if (fabs(joystickDY) > 0.1f) {
        moveDir += horizontalFront * (joystickDY < -0.1f ? -joystickDY : -joystickDY);
    }
    if (fabs(joystickDX) > 0.1f) {
        moveDir += right * (joystickDX > 0.1f ? joystickDX : joystickDX);
    }

    // ---- 水平输入 → 速度 ----
    if (glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
        velocity.x += moveDir.x * MOVE_ACCELERATION;
        velocity.z += moveDir.z * MOVE_ACCELERATION;
    }

    // 地面摩擦力 / 空气阻力
    if (onGround) {
        velocity.x *= 0.6f;
        velocity.z *= 0.6f;
    } else {
        velocity.x *= 0.98f;
        velocity.z *= 0.98f;
    }

    // 限制水平速度
    float horizSpeed = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
    if (horizSpeed > MOVE_SPEED) {
        velocity.x = velocity.x / horizSpeed * MOVE_SPEED;
        velocity.z = velocity.z / horizSpeed * MOVE_SPEED;
    }

    // ---- 跳跃（在移动前设置速度）----
    if (jumpPressed && onGround) {
        velocity.y = JUMP_VELOCITY;
        onGround = false;
        jumpPressed = false;
    }

    // ---- 碰撞处理（原版Minecraft顺序：Y → X → Z，每轴后立即更新位置）----
    // 重力在 Y 轴移动后应用，保证跳跃 tick 使用完整跳跃速度
    float preVelY = velocity.y;

    // Y轴（垂直优先）
    if (velocity.y != 0.0f) {
        AABB box = getPlayerAABB().offset(0.0f, velocity.y, 0.0f);
        int minBX = (int)floorf(box.minX);
        int maxBX = (int)floorf(box.maxX - 1e-5f);
        int minBY = (int)floorf(box.minY);
        int maxBY = (int)floorf(box.maxY - 1e-5f);
        int minBZ = (int)floorf(box.minZ);
        int maxBZ = (int)floorf(box.maxZ - 1e-5f);

        AABB playerBox = getPlayerAABB();
        for (int by = minBY; by <= maxBY; by++) {
            for (int bz = minBZ; bz <= maxBZ; bz++) {
                for (int bx = minBX; bx <= maxBX; bx++) {
                    if (!isBlockSolid(bx, by, bz)) continue;
                    float bh = getBlockHeight(bx, by, bz);
                    if (bh <= 0.0f) continue;

                    AABB blockBox((float)bx, (float)by, (float)bz,
                                  (float)(bx + 1), (float)by + bh, (float)(bz + 1));
                    if (!box.intersects(blockBox)) continue;

                    if (velocity.y > 0.0f && playerBox.maxY <= blockBox.minY) {
                        float newDy = blockBox.minY - playerBox.maxY;
                        if (newDy < velocity.y) velocity.y = newDy;
                    } else if (velocity.y < 0.0f && playerBox.minY >= blockBox.maxY) {
                        float newDy = blockBox.maxY - playerBox.minY;
                        if (newDy > velocity.y) velocity.y = newDy;
                    }
                }
            }
        }
    }
    float actualY = velocity.y;
    position.y += actualY;

    // 重力（基于碰撞前的速度计算下一 tick 的垂直速度）
    velocity.y = preVelY - GRAVITY;
    if (velocity.y < -3.0f) velocity.y = -3.0f;

    // X轴（水平）
    if (velocity.x != 0.0f) {
        AABB box = getPlayerAABB().offset(velocity.x, 0.0f, 0.0f);
        int minBX = (int)floorf(box.minX);
        int maxBX = (int)floorf(box.maxX - 1e-5f);
        int minBY = (int)floorf(box.minY);
        int maxBY = (int)floorf(box.maxY - 1e-5f);
        int minBZ = (int)floorf(box.minZ);
        int maxBZ = (int)floorf(box.maxZ - 1e-5f);

        AABB playerBox = getPlayerAABB();
        for (int by = minBY; by <= maxBY; by++) {
            for (int bz = minBZ; bz <= maxBZ; bz++) {
                for (int bx = minBX; bx <= maxBX; bx++) {
                    if (!isBlockSolid(bx, by, bz)) continue;
                    float bh = getBlockHeight(bx, by, bz);
                    if (bh <= 0.0f) continue;

                    AABB blockBox((float)bx, (float)by, (float)bz,
                                  (float)(bx + 1), (float)by + bh, (float)(bz + 1));
                    if (!box.intersects(blockBox)) continue;

                    if (velocity.x > 0.0f && playerBox.maxX <= blockBox.minX) {
                        float newDx = blockBox.minX - playerBox.maxX;
                        if (newDx < velocity.x) velocity.x = newDx;
                    } else if (velocity.x < 0.0f && playerBox.minX >= blockBox.maxX) {
                        float newDx = blockBox.maxX - playerBox.minX;
                        if (newDx > velocity.x) velocity.x = newDx;
                    }
                }
            }
        }
    }
    position.x += velocity.x;

    // Z轴（水平）
    if (velocity.z != 0.0f) {
        AABB box = getPlayerAABB().offset(0.0f, 0.0f, velocity.z);
        int minBX = (int)floorf(box.minX);
        int maxBX = (int)floorf(box.maxX - 1e-5f);
        int minBY = (int)floorf(box.minY);
        int maxBY = (int)floorf(box.maxY - 1e-5f);
        int minBZ = (int)floorf(box.minZ);
        int maxBZ = (int)floorf(box.maxZ - 1e-5f);

        AABB playerBox = getPlayerAABB();
        for (int by = minBY; by <= maxBY; by++) {
            for (int bz = minBZ; bz <= maxBZ; bz++) {
                for (int bx = minBX; bx <= maxBX; bx++) {
                    if (!isBlockSolid(bx, by, bz)) continue;
                    float bh = getBlockHeight(bx, by, bz);
                    if (bh <= 0.0f) continue;

                    AABB blockBox((float)bx, (float)by, (float)bz,
                                  (float)(bx + 1), (float)by + bh, (float)(bz + 1));
                    if (!box.intersects(blockBox)) continue;

                    if (velocity.z > 0.0f && playerBox.maxZ <= blockBox.minZ) {
                        float newDz = blockBox.minZ - playerBox.maxZ;
                        if (newDz < velocity.z) velocity.z = newDz;
                    } else if (velocity.z < 0.0f && playerBox.minZ >= blockBox.maxZ) {
                        float newDz = blockBox.maxZ - playerBox.minZ;
                        if (newDz > velocity.z) velocity.z = newDz;
                    }
                }
            }
        }
    }
    position.z += velocity.z;

    // ---- 更新地面状态 ----
    if (actualY != preVelY && preVelY < 0) {
        onGround = true;
        velocity.y = 0.0f;
    } else if (preVelY <= 0.0f && actualY == 0.0f) {
        onGround = true;
    } else {
        onGround = false;
    }

    // 摔出世界保护
    if (position.y < -64.0f) {
        position.y = 320.0f;
        velocity = glm::vec3(0.0f);
        onGround = false;
    }
}

// ===== 碰撞检测 =====

AABB Collision::getPlayerAABB() const {
    float hw = PLAYER_WIDTH * 0.5f;
    return AABB(position.x - hw, position.y, position.z - hw,
                position.x + hw, position.y + PLAYER_HEIGHT, position.z + hw);
}

bool Collision::isBlockSolid(int blockX, int blockY, int blockZ) const {
    if (!chunkManager) return false;
    auto chunk = chunkManager->getChunk(blockX >> 4, blockZ >> 4);
    if (!chunk || !chunk->isLoaded) return false;

    int32_t state = chunk->getBlockState(blockX & 15, blockY, blockZ & 15);
    if (state == 0) return false;

    const auto& meta = BlockRegistry::getInstance().getBlockMetadata(state);
    return !meta.isPlant && !meta.isWater;
}

float Collision::getBlockHeight(int blockX, int blockY, int blockZ) const {
    if (!chunkManager) return 1.0f;
    auto chunk = chunkManager->getChunk(blockX >> 4, blockZ >> 4);
    if (!chunk || !chunk->isLoaded) return 1.0f;

    int32_t state = chunk->getBlockState(blockX & 15, blockY, blockZ & 15);
    if (state == 0) return 0.0f;

    const auto& meta = BlockRegistry::getInstance().getBlockMetadata(state);
    if (meta.isPlant || meta.isWater) return 0.0f;
    return meta.height;
}

// ===== 访问器 =====

glm::vec3 Collision::getPosition() const {
    std::lock_guard<std::mutex> lock(mutex);
    return position;
}

glm::vec3 Collision::getSmoothPosition() const {
    std::lock_guard<std::mutex> lock(mutex);
    float alpha = accumulatedTime / TICK_DURATION;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    return prevPosition + (position - prevPosition) * alpha;
}

void Collision::setPosition(float x, float y, float z) {
    std::lock_guard<std::mutex> lock(mutex);
    position = glm::vec3(x, y, z);
    velocity = glm::vec3(0.0f);
    onGround = false;
    LOGI("Player position set to (%.2f, %.2f, %.2f)", x, y, z);
}
