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
#define KEY_SPRINT 6

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
        case KEY_SPRINT:
            if (pressed) keySprint = !keySprint;  // 点按切换
            LOGI("Sprint toggled: %s", keySprint ? "ON" : "OFF");
            break;
    }
}

void Collision::setJoystickInput(float dx, float dy) {
    std::lock_guard<std::mutex> lock(mutex);
    joystickDX = dx;
    joystickDY = dy;
}

// ===== 物理更新 =====

void Collision::update(float deltaTime, float camPitch, float camYaw, glm::vec3* outPosition, bool* outOnGround) {
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

    if (outPosition) *outPosition = position;
    if (outOnGround) *outOnGround = onGround;
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

    // ===== 旁观者模式：无视碰撞，自由飞行 =====
    if (noClip) {
        const float SPECTATOR_SPEED = 0.5f;
        glm::vec3 specDir(0.0f);
        if (keyW) specDir += front;
        if (keyS) specDir -= front;
        if (keyA) specDir -= right;
        if (keyD) specDir += right;
        if (fabs(joystickDY) > 0.1f) specDir += front * -joystickDY;
        if (fabs(joystickDX) > 0.1f) specDir += right * joystickDX;
        if (glm::length(specDir) > 0.001f) {
            specDir = glm::normalize(specDir);
            position += specDir * SPECTATOR_SPEED;
        }
        if (jumpPressed) position.y += SPECTATOR_SPEED;
        if (keyDown) position.y -= SPECTATOR_SPEED;
        velocity = glm::vec3(0.0f);
        onGround = false;
        prevPosition = position;
        return;
    }

    // ---- 水平输入 → 速度 ----
    // 疾跑判定：按下疾跑键且正在向前移动
    bool sprinting = keySprint && (keyW || joystickDY < -0.1f);
    float accel = sprinting ? SPRINT_MOVE_ACCELERATION : MOVE_ACCELERATION;
    float speedCap = sprinting ? SPRINT_MOVE_SPEED : MOVE_SPEED;

    if (glm::length(moveDir) > 0.001f) {
        moveDir = glm::normalize(moveDir);
        velocity.x += moveDir.x * accel;
        velocity.z += moveDir.z * accel;
    }

    // ---- 创造模式飞行 ----
    bool isFlying = (gameMode == 1 || gameMode == 3);
    if (isFlying) {
        // 垂直控制（升/降）
        if (jumpPressed) velocity.y = 0.4f;
        else if (keyDown) velocity.y = -0.4f;
        else velocity.y *= 0.6f;

        // 水平：飞行时施加阻力（松开按键后逐渐停下）
        if (glm::length(moveDir) < 0.001f) {
            velocity.x *= 0.8f;
            velocity.z *= 0.8f;
            if (fabs(velocity.x) < 0.001f) velocity.x = 0.0f;
            if (fabs(velocity.z) < 0.001f) velocity.z = 0.0f;
        }
        speedCap = 0.5f;
        float hSpeed = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
        if (hSpeed > speedCap) {
            velocity.x = velocity.x / hSpeed * speedCap;
            velocity.z = velocity.z / hSpeed * speedCap;
        }
        onGround = false;
    } else {
        // ---- 地面摩擦力 / 空气阻力（仅非飞行）----
        if (onGround) {
            velocity.x *= 0.6f;
            velocity.z *= 0.6f;
        } else {
            velocity.x *= 0.98f;
            velocity.z *= 0.98f;
        }

        // 限制水平速度
        float horizSpeed = sqrtf(velocity.x * velocity.x + velocity.z * velocity.z);
        if (horizSpeed > speedCap) {
            velocity.x = velocity.x / horizSpeed * speedCap;
            velocity.z = velocity.z / horizSpeed * speedCap;
        }

        // ---- 跳跃 ----
        if (jumpPressed && onGround) {
            velocity.y = JUMP_VELOCITY;
            onGround = false;
            jumpPressed = false;
        }
    }

    // ---- 碰撞处理（原版Minecraft顺序：Y → X → Z，每轴后立即更新位置）----
    // 重力在 Y 轴移动后应用，保证跳跃 tick 使用完整跳跃速度
    float preVelY = velocity.y;

    // Y轴（垂直优先）
    if (velocity.y != 0.0f && !isFlying) {
        AABB box = getPlayerAABB().offset(0.0f, velocity.y, 0.0f);
        int minBX = (int)floorf(box.minX);
        int maxBX = (int)floorf(box.maxX + 1e-5f);
        int minBY = (int)floorf(box.minY);
        int maxBY = (int)floorf(box.maxY + 1e-5f);
        int minBZ = (int)floorf(box.minZ);
        int maxBZ = (int)floorf(box.maxZ + 1e-5f);

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

    if (!isFlying) {
        velocity.y = preVelY - GRAVITY;
        if (velocity.y < -3.0f) velocity.y = -3.0f;
    }

    // 自动踏步标记（如果 X 踏步成功，Z 不再重复抬高）
    bool steppedUp = false;

    // X轴（水平）— 带自动踏步
    if (velocity.x != 0.0f) {
        float desiredVelX = velocity.x;
        float origY = position.y;

        AABB box = getPlayerAABB().offset(velocity.x, 0.0f, 0.0f);
        int minBX = (int)floorf(box.minX);
        int maxBX = (int)floorf(box.maxX + 1e-5f);
        int minBY = (int)floorf(box.minY);
        int maxBY = (int)floorf(box.maxY + 1e-5f);
        int minBZ = (int)floorf(box.minZ);
        int maxBZ = (int)floorf(box.maxZ + 1e-5f);

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

                    if (velocity.x > 0.0f && playerBox.maxX <= blockBox.minX + 1e-5f) {
                        float newDx = blockBox.minX - playerBox.maxX - 0.001f;
                        if (newDx < velocity.x) velocity.x = newDx;
                    } else if (velocity.x < 0.0f && playerBox.minX >= blockBox.maxX - 1e-5f) {
                        float newDx = blockBox.maxX - playerBox.minX + 0.001f;
                        if (newDx > velocity.x) velocity.x = newDx;
                    }
                }
            }
        }

        // 自动踏步：在地面上且被阻挡时，尝试抬腿
        if (onGround && fabsf(velocity.x) < fabsf(desiredVelX) - 1e-7f) {
            position.y = origY + STEP_HEIGHT;
            bool stepClear = true;
            AABB stepBox = getPlayerAABB().offset(desiredVelX, 0.0f, 0.0f);
            int sminBX = (int)floorf(stepBox.minX);
            int smaxBX = (int)floorf(stepBox.maxX + 1e-5f);
            int sminBY = (int)floorf(stepBox.minY);
            int smaxBY = (int)floorf(stepBox.maxY + 1e-5f);
            int sminBZ = (int)floorf(stepBox.minZ);
            int smaxBZ = (int)floorf(stepBox.maxZ + 1e-5f);
            for (int by = sminBY; by <= smaxBY && stepClear; by++) {
                for (int bz = sminBZ; bz <= smaxBZ && stepClear; bz++) {
                    for (int bx = sminBX; bx <= smaxBX && stepClear; bx++) {
                        if (!isBlockSolid(bx, by, bz)) continue;
                        float bh = getBlockHeight(bx, by, bz);
                        if (bh <= 0.0f) continue;
                        AABB blockBox((float)bx, (float)by, (float)bz,
                                      (float)(bx + 1), (float)by + bh, (float)(bz + 1));
                        if (stepBox.intersects(blockBox)) stepClear = false;
                    }
                }
            }
            if (stepClear) {
                // 找到目标位置的实际地面高度，精确落在地面上
                float newGround = origY;
                for (int by = (int)floorf(origY); by <= (int)floorf(origY + STEP_HEIGHT); by++) {
                    for (int bz = sminBZ; bz <= smaxBZ; bz++) {
                        for (int bx = sminBX; bx <= smaxBX; bx++) {
                            if (!isBlockSolid(bx, by, bz)) continue;
                            float bh = getBlockHeight(bx, by, bz);
                            float top = (float)by + bh;
                            if (top > newGround && top <= origY + STEP_HEIGHT + 1e-4f) {
                                newGround = top;
                            }
                        }
                    }
                }
                velocity.x = desiredVelX;
                steppedUp = true;
                position.y = newGround;
            } else {
                position.y = origY;
            }
        }

        position.x += velocity.x;
    }

    // Z轴（水平）— 带自动踏步
    if (velocity.z != 0.0f) {
        float desiredVelZ = velocity.z;
        float origY = position.y;

        AABB box = getPlayerAABB().offset(0.0f, 0.0f, velocity.z);
        int minBX = (int)floorf(box.minX);
        int maxBX = (int)floorf(box.maxX + 1e-5f);
        int minBY = (int)floorf(box.minY);
        int maxBY = (int)floorf(box.maxY + 1e-5f);
        int minBZ = (int)floorf(box.minZ);
        int maxBZ = (int)floorf(box.maxZ + 1e-5f);

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

                    if (velocity.z > 0.0f && playerBox.maxZ <= blockBox.minZ + 1e-5f) {
                        float newDz = blockBox.minZ - playerBox.maxZ - 0.001f;
                        if (newDz < velocity.z) velocity.z = newDz;
                    } else if (velocity.z < 0.0f && playerBox.minZ >= blockBox.maxZ - 1e-5f) {
                        float newDz = blockBox.maxZ - playerBox.minZ + 0.001f;
                        if (newDz > velocity.z) velocity.z = newDz;
                    }
                }
            }
        }

        // 自动踏步：仅当 X 轴未踏步时尝试（否则 Y 已被抬高，Z 在已抬高位置重测过）
        if (onGround && !steppedUp && fabsf(velocity.z) < fabsf(desiredVelZ) - 1e-7f) {
            position.y = origY + STEP_HEIGHT;
            bool stepClear = true;
            AABB stepBox = getPlayerAABB().offset(0.0f, 0.0f, desiredVelZ);
            int sminBX = (int)floorf(stepBox.minX);
            int smaxBX = (int)floorf(stepBox.maxX + 1e-5f);
            int sminBY = (int)floorf(stepBox.minY);
            int smaxBY = (int)floorf(stepBox.maxY + 1e-5f);
            int sminBZ = (int)floorf(stepBox.minZ);
            int smaxBZ = (int)floorf(stepBox.maxZ + 1e-5f);
            for (int by = sminBY; by <= smaxBY && stepClear; by++) {
                for (int bz = sminBZ; bz <= smaxBZ && stepClear; bz++) {
                    for (int bx = sminBX; bx <= smaxBX && stepClear; bx++) {
                        if (!isBlockSolid(bx, by, bz)) continue;
                        float bh = getBlockHeight(bx, by, bz);
                        if (bh <= 0.0f) continue;
                        AABB blockBox((float)bx, (float)by, (float)bz,
                                      (float)(bx + 1), (float)by + bh, (float)(bz + 1));
                        if (stepBox.intersects(blockBox)) stepClear = false;
                    }
                }
            }
            if (stepClear) {
                // 找到目标位置的实际地面高度
                float newGround = origY;
                for (int by = (int)floorf(origY); by <= (int)floorf(origY + STEP_HEIGHT); by++) {
                    for (int bz = sminBZ; bz <= smaxBZ; bz++) {
                        for (int bx = sminBX; bx <= smaxBX; bx++) {
                            if (!isBlockSolid(bx, by, bz)) continue;
                            float bh = getBlockHeight(bx, by, bz);
                            float top = (float)by + bh;
                            if (top > newGround && top <= origY + STEP_HEIGHT + 1e-4f) {
                                newGround = top;
                            }
                        }
                    }
                }
                velocity.z = desiredVelZ;
                position.y = newGround;
            } else {
                position.y = origY;
            }
        }

        position.z += velocity.z;
    }

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
        return;
    }

    // ---- 安全网：强制将玩家推出任何重叠的方块 ----
    {
        AABB pBox = getPlayerAABB();
        int pMinX, pMaxX, pMinY, pMaxY, pMinZ, pMaxZ;

    restart:
        pBox = getPlayerAABB();
        pMinX = (int)floorf(pBox.minX);
        pMaxX = (int)floorf(pBox.maxX + 1e-5f);
        pMinY = (int)floorf(pBox.minY);
        pMaxY = (int)floorf(pBox.maxY + 1e-5f);
        pMinZ = (int)floorf(pBox.minZ);
        pMaxZ = (int)floorf(pBox.maxZ + 1e-5f);

        for (int by = pMinY; by <= pMaxY; by++) {
            for (int bz = pMinZ; bz <= pMaxZ; bz++) {
                for (int bx = pMinX; bx <= pMaxX; bx++) {
                    if (!isBlockSolid(bx, by, bz)) continue;
                    float bh = getBlockHeight(bx, by, bz);
                    if (bh <= 0.0f) continue;

                    AABB blockBox((float)bx, (float)by, (float)bz,
                                  (float)(bx + 1), (float)by + bh, (float)(bz + 1));
                    if (!pBox.intersects(blockBox)) continue;

                    // 玩家与方块重叠，沿阻力最小的方向推出
                    float pushLeft  = pBox.maxX - blockBox.minX;
                    float pushRight = blockBox.maxX - pBox.minX;
                    float pushDown  = pBox.maxY - blockBox.minY;
                    float pushUp    = blockBox.maxY - pBox.minY;
                    float pushBack  = pBox.maxZ - blockBox.minZ;
                    float pushFront = blockBox.maxZ - pBox.minZ;

                    float minOverlap = pushLeft;
                    int axis = 0; float displace = -pushLeft;
                    if (pushRight < minOverlap) { minOverlap = pushRight; axis = 0; displace = pushRight; }
                    if (pushDown < minOverlap)  { minOverlap = pushDown;  axis = 1; displace = -pushDown; }
                    if (pushUp < minOverlap)    { minOverlap = pushUp;    axis = 1; displace = pushUp; }
                    if (pushBack < minOverlap)  { minOverlap = pushBack;  axis = 2; displace = -pushBack; }
                    if (pushFront < minOverlap) { minOverlap = pushFront; axis = 2; displace = pushFront; }

                    if (axis == 0) {
                        position.x += displace;
                        velocity.x = 0.0f;
                    } else if (axis == 1) {
                        position.y += displace;
                        velocity.y = 0.0f;
                        if (displace > 0) onGround = true;
                    } else if (axis == 2) {
                        position.z += displace;
                        velocity.z = 0.0f;
                    }

                    goto restart;  // 位置变了，重新扫描所有方块
                }
            }
        }
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

glm::vec3 Collision::getVelocity() const {
    std::lock_guard<std::mutex> lock(mutex);
    return velocity;
}

glm::vec3 Collision::getSmoothPosition() const {
    std::lock_guard<std::mutex> lock(mutex);
    float alpha = accumulatedTime / TICK_DURATION;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;
    return prevPosition + (position - prevPosition) * alpha;
}

bool Collision::isOnGround() const {
    std::lock_guard<std::mutex> lock(mutex);
    return onGround;
}

void Collision::setPosition(float x, float y, float z) {
    std::lock_guard<std::mutex> lock(mutex);
    position = glm::vec3(x, y, z);
    velocity = glm::vec3(0.0f);
    onGround = false;
    LOGI("Player position set to (%.2f, %.2f, %.2f)", x, y, z);
}

void Collision::setGameMode(int mode) {
    std::lock_guard<std::mutex> lock(mutex);
    gameMode = mode;
    noClip = (mode == 3);
    LOGI("Game mode set to %d (noClip=%s)", mode, noClip ? "true" : "false");
}
