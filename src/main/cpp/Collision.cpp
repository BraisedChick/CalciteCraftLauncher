#include "Collision.h"
#include "ClientEngine/ClientEngine.h"
#include "ClientEngine/GameEngine.h"
#include "EntityManager.h"
#include "ChunkManager.h"
#include "BlockRegistry.h"
#include "TextureAtlas.h"
#include <glm/gtc/matrix_transform.hpp>
#include <android/log.h>
#include <cmath>
#include <algorithm>

#define LOG_TAG "Collision"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define KEY_W 0
#define KEY_S 1
#define KEY_A 2
#define KEY_D 3
#define KEY_UP 4
#define KEY_DOWN 5
#define KEY_SPRINT 6

// ---------- 外部接口 ----------
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
            if (pressed) keySprint = !keySprint;
            LOGI("Sprint toggled: %s", keySprint ? "ON" : "OFF");
            break;
    }
}

void Collision::setJoystickInput(float dx, float dy) {
    joystickDX.store(dx);
    joystickDY.store(dy);
}

void Collision::resetMovement() {
    std::lock_guard<std::mutex> lock(mutex);
    keyW = keyS = keyA = keyD = false;
    keyUp = keyDown = false;
    keySprint = false;
    jumpPressed = false;
    joystickDX.store(0.0f);
    joystickDY.store(0.0f);
}

void Collision::update(float deltaTime, float camPitch, float camYaw, glm::vec3* outPosition, bool* outOnGround) {
    std::lock_guard<std::mutex> lock(mutex);

    pitch = camPitch;
    yaw = camYaw;

    // 区块未加载则暂停
    if (chunkManager) {
        int cx = (int)floorf(position.x) >> 4;
        int cz = (int)floorf(position.z) >> 4;
        auto chunk = chunkManager->getChunk(cx, cz);
        if (!chunk || !chunk->isLoaded) {
            accumulatedTime = 0.0f;
            if (outPosition) *outPosition = position;
            if (outOnGround) *outOnGround = onGround;
            return;
        }
    }

    // 击退
    if (playerEntityId >= 0) {
        auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
        if (game) {
            double kbVx=0, kbVy=0, kbVz=0;
            game->getEntityManager()->consumeEntityMotion(playerEntityId, kbVx, kbVy, kbVz);
            velocity.x += (float)kbVx;
            velocity.y += (float)kbVy;
            velocity.z += (float)kbVz;
        }
    }

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

// ---------- 物理核心----------
void Collision::tick() {
    if (!chunkManager) return;
    ClientEngine::getInstance()->getGame()->getEntityManager()->tick(1);
    // 旁观者模式（无碰撞飞行）
    if (noClip) {
        const float SPEED = 0.5f;
        glm::vec3 front, right;
        float cosYaw = cosf(yaw), sinYaw = sinf(yaw);
        float cosPitch = cosf(pitch), sinPitch = sinf(pitch);
        front = glm::normalize(glm::vec3(-sinYaw * cosPitch, -sinPitch, cosYaw * cosPitch));
        right = glm::normalize(glm::cross(front, glm::vec3(0,1,0)));

        glm::vec3 move(0,0,0);
        if (keyW) move += front;
        if (keyS) move -= front;
        if (keyA) move -= right;
        if (keyD) move += right;
        float jdy = joystickDY.load();
        if (fabs(jdy) > 0.1f) move += front * (-jdy);
        float jdx = joystickDX.load();
        if (fabs(jdx) > 0.1f) move += right * jdx;
        if (glm::length(move) > 0.001f) move = glm::normalize(move) * SPEED;
        if (jumpPressed) move.y += SPEED;
        if (keyDown) move.y -= SPEED;
        position += move;
        velocity = glm::vec3(0);
        onGround = false;
        return;
    }

    // 1. 调用 movePlayer (LivingEntity::travel)
    movePlayer();

    // 2. 边界限位（原版有，防止超界）
    position.x = std::clamp(position.x, -2.9999999E7f, 2.9999999E7f);
    position.z = std::clamp(position.z, -2.9999999E7f, 2.9999999E7f);

    ClientEngine::getInstance()->getGame()->getEntityManager()->tick(1);
}

// ---------- LivingEntity::travel 等价 ----------
void Collision::movePlayer() {
    // 省略水中/岩浆/鞘翅等特殊情况，仅实现普通地面移动
    // 若有这些需求可后续补充

    // 重力（非飞行时）
    if (!isFlying) {
        velocity.y -= GRAVITY;
        if (velocity.y < -3.0f) velocity.y = -3.0f;
    }

    // 输入加速度
    applyInputs(isFlying ? 0.02f : (onGround ? 0.216f : 0.02f)); // 简化，实际需根据摩擦计算

    // 跳跃
    if (jumpPressed && onGround && !isFlying) {
        velocity.y = JUMP_VELOCITY;
        onGround = false;
        jumpPressed = false;
    }

    // 飞行垂直控制
    if (isFlying) {
        if (jumpPressed) velocity.y = 0.4f;
        else if (keyDown) velocity.y = -0.4f;
        else velocity.y *= 0.6f;
    }

    // 限速
    float horiz = sqrtf(velocity.x*velocity.x + velocity.z*velocity.z);
    float speedCap = keySprint ? SPRINT_MOVE_SPEED : MOVE_SPEED;
    if (isFlying) speedCap = 0.5f;
    if (horiz > speedCap) {
        velocity.x = velocity.x / horiz * speedCap;
        velocity.z = velocity.z / horiz * speedCap;
    }

    // 执行移动
    applyMovement();

    // 水平摩擦
    if (onGround) {
        velocity.x *= 0.6f;
        velocity.z *= 0.6f;
    } else {
        velocity.x *= 0.98f;
        velocity.z *= 0.98f;
    }

    // 微小速度归零
    if (fabsf(velocity.x) < 0.003f) velocity.x = 0.0f;
    if (fabsf(velocity.y) < 0.003f) velocity.y = 0.0f;
    if (fabsf(velocity.z) < 0.003f) velocity.z = 0.0f;

    // 检查特殊方块（粘液块、灵魂沙等）
    checkInsideBlocks();
}

// ---------- Entity::move 等价 ----------
void Collision::applyMovement() {
    if (noClip) return;

    glm::vec3 movement = velocity;

    if (glm::length(stuckSpeedMultiplier - glm::vec3(1.0f)) > EPSILON) {
        movement *= stuckSpeedMultiplier;
        stuckSpeedMultiplier = glm::vec3(1.0f);
        velocity = glm::vec3(0.0f);
    }

    AABB playerAABB = getPlayerAABB();
    glm::vec3 movementBefore = movement;

    // 1. 碰撞检测
    if (glm::length(movement) > 0.0f) {
        movement = collideBoundingBox(playerAABB, movement);
    }

    // 2. 简化踏步逻辑
    if ((onGround || (movement.y != movementBefore.y && movementBefore.y < 0.0f)) &&
        (movement.x != movementBefore.x || movement.z != movementBefore.z)) {

        // 尝试抬高 STEP_HEIGHT (0.6) 并水平移动
        glm::vec3 stepUpMovement = collideBoundingBox(playerAABB,
                                                      glm::vec3(movementBefore.x, STEP_HEIGHT, movementBefore.z));
        // 如果踏步后的水平位移更远，则应用踏步
        if (stepUpMovement.x * stepUpMovement.x + stepUpMovement.z * stepUpMovement.z >
            movement.x * movement.x + movement.z * movement.z) {
            // 应用踏步位移，并处理竖直下降
            movement = stepUpMovement +
                       collideBoundingBox(playerAABB.offset(stepUpMovement.x, stepUpMovement.y, stepUpMovement.z),
                                          glm::vec3(0.0f, -stepUpMovement.y + movementBefore.y, 0.0f));
        }
    }

    // 3. 应用位移
    if (glm::length(movement) > EPSILON) {
        position += movement;
    }

    // 4. 碰撞轴与地面判定
    bool collisionX = movementBefore.x != movement.x;
    bool collisionY = movementBefore.y != movement.y;
    bool collisionZ = movementBefore.z != movement.z;
    horizontalCollision = collisionX || collisionZ;

    onGround = (movementBefore.y < 0.0f && collisionY);

    // 5. 速度归零
    if (collisionX) velocity.x = 0.0f;
    if (collisionZ) velocity.z = 0.0f;
    if (collisionY) {
        velocity.y = 0.0f; // 简化：蹲下不处理
    }
}
// ---------- AABB 碰撞检测----------
glm::vec3 Collision::collideBoundingBox(const AABB& aabb, const glm::vec3& movement) const {
    // 1. 使用膨胀的 AABB 收集碰撞箱
    glm::vec3 center = aabb.getCenter();
    glm::vec3 half = aabb.getHalfSize();
    glm::vec3 newCenter = center + movement * 0.5f;
    glm::vec3 newHalf = half + glm::abs(movement) * 0.5f;
    AABB movementExtended(newCenter.x - newHalf.x, newCenter.y - newHalf.y, newCenter.z - newHalf.z,
                          newCenter.x + newHalf.x, newCenter.y + newHalf.y, newCenter.z + newHalf.z);

    std::vector<AABB> colliders;
    int minX = (int)floorf(movementExtended.minX);
    int maxX = (int)floorf(movementExtended.maxX + EPSILON);
    int minY = (int)floorf(movementExtended.minY) - 1;  // 注意：减 1
    int maxY = (int)floorf(movementExtended.maxY + EPSILON);
    int minZ = (int)floorf(movementExtended.minZ);
    int maxZ = (int)floorf(movementExtended.maxZ + EPSILON);

    for (int y = minY; y <= maxY; ++y)
        for (int z = minZ; z <= maxZ; ++z)
            for (int x = minX; x <= maxX; ++x) {
                auto boxes = getBlockAABBs(x, y, z);
                colliders.insert(colliders.end(), boxes.begin(), boxes.end());
            }

    if (colliders.empty()) return movement;

    // 2. 使用原始 aabb 进行碰撞修正（与之前相同）
    glm::vec3 collidedMovement = movement;
    AABB movedAABB = aabb;
    collideOneAxis(movedAABB, collidedMovement, 1, colliders);
    if (fabsf(collidedMovement.x) > fabsf(collidedMovement.z)) {
        collideOneAxis(movedAABB, collidedMovement, 0, colliders);
        collideOneAxis(movedAABB, collidedMovement, 2, colliders);
    } else {
        collideOneAxis(movedAABB, collidedMovement, 2, colliders);
        collideOneAxis(movedAABB, collidedMovement, 0, colliders);
    }

    return collidedMovement;
}

void Collision::collideOneAxis(AABB& aabb, glm::vec3& movement, int axis, const std::vector<AABB>& colliders) const {
    if (fabsf(movement[axis]) < EPSILON) {
        movement[axis] = 0.0f;
        return;
    }

    int axis1 = (axis + 1) % 3;
    int axis2 = (axis + 2) % 3;

    float aabbMin[3] = {aabb.minX, aabb.minY, aabb.minZ};
    float aabbMax[3] = {aabb.maxX, aabb.maxY, aabb.maxZ};

    for (const auto& collider : colliders) {
        float collMin[3] = {collider.minX, collider.minY, collider.minZ};
        float collMax[3] = {collider.maxX, collider.maxY, collider.maxZ};

        // 检查其他两个轴是否重叠
        if (aabbMax[axis1] - EPSILON > collMin[axis1] &&
            aabbMin[axis1] + EPSILON < collMax[axis1] &&
            aabbMax[axis2] - EPSILON > collMin[axis2] &&
            aabbMin[axis2] + EPSILON < collMax[axis2]) {

            if (movement[axis] > 0.0f) {
                // 只有方块在玩家上方才修正
                if (aabbMax[axis] - EPSILON <= collMin[axis]) {
                    float newMove = collMin[axis] - aabbMax[axis];
                    if (newMove < movement[axis]) movement[axis] = newMove;
                }
            } else { // movement[axis] < 0.0f
                // 只有方块在玩家下方才修正
                if (aabbMin[axis] + EPSILON >= collMax[axis]) {
                    float newMove = collMax[axis] - aabbMin[axis];
                    if (newMove > movement[axis]) movement[axis] = newMove;
                }
            }
        }
    }

    glm::vec3 translation(0.0f, 0.0f, 0.0f);
    translation[axis] = movement[axis];
    aabb.translate(translation);
}

// ---------- 特殊方块检查 ----------
void Collision::checkInsideBlocks() {
    // 实现蜘蛛网、粘液块等，此处略，可后续添加
}

// ---------- 输入处理 ----------
glm::vec3 Collision::getInputVector() const {
    glm::vec3 front, right;
    float cosYaw = cosf(yaw), sinYaw = sinf(yaw);
    float cosPitch = cosf(pitch), sinPitch = sinf(pitch);
    front = glm::normalize(glm::vec3(-sinYaw * cosPitch, -sinPitch, cosYaw * cosPitch));
    right = glm::normalize(glm::cross(front, glm::vec3(0,1,0)));

    glm::vec3 move(0,0,0);
    if (keyW) move += glm::vec3(front.x, 0, front.z);
    if (keyS) move -= glm::vec3(front.x, 0, front.z);
    if (keyA) move -= right;
    if (keyD) move += right;
    float jdy = joystickDY.load();
    if (fabs(jdy) > 0.1f) move += glm::vec3(front.x, 0, front.z) * (-jdy);
    float jdx = joystickDX.load();
    if (fabs(jdx) > 0.1f) move += right * jdx;

    if (glm::length(move) > 1.0f) move = glm::normalize(move);
    return move;
}

void Collision::applyInputs(float strength) {
    glm::vec3 input = getInputVector();
    if (glm::length(input) < EPSILON) return;

    // 直接累加，因为 input 已经是世界坐标系下的方向
    velocity.x += input.x * strength;
    velocity.z += input.z * strength;
}

// ---------- AABB 获取 ----------
AABB Collision::getPlayerAABB() const {
    float hw = PLAYER_WIDTH * 0.5f;
    return AABB(position.x - hw, position.y, position.z - hw,
                position.x + hw, position.y + PLAYER_HEIGHT, position.z + hw);
}

std::vector<AABB> Collision::getBlockAABBs(int blockX, int blockY, int blockZ) const {
    if (!chunkManager) {
        return {AABB((float)blockX, (float)blockY, (float)blockZ,
                     (float)(blockX+1), (float)(blockY+1), (float)(blockZ+1))};
    }
    auto chunk = chunkManager->getChunk(blockX >> 4, blockZ >> 4);
    if (!chunk || !chunk->isLoaded) {
        return {AABB((float)blockX, (float)blockY, (float)blockZ,
                     (float)(blockX+1), (float)(blockY+1), (float)(blockZ+1))};
    }
    int32_t state = chunk->getBlockState(blockX & 15, blockY, blockZ & 15);
    if (state == 0) return {};

    const auto& meta = ClientEngine::getInstance()->getBlockRegistry()->getBlockMetadata(state);
    if (meta.isPlant || meta.isWater || meta.isNoCollision) return {};

    auto* atlas = ClientEngine::getInstance()->getTextureAtlas();
    if (!atlas || !atlas->isInitialized()) {
        float h = meta.height;
        if (h <= 0.0f) return {};
        return {AABB((float)blockX, (float)blockY, (float)blockZ,
                     (float)(blockX+1), (float)blockY + h, (float)(blockZ+1))};
    }

    auto boxes = atlas->getBlockCollisionBoxes(meta.name, state, meta.minStateId);
    if (boxes.empty()) {
        return {AABB((float)blockX, (float)blockY, (float)blockZ,
                     (float)(blockX+1), (float)(blockY+1), (float)(blockZ+1))};
    }
    std::vector<AABB> result;
    result.reserve(boxes.size());
    for (const auto& cb : boxes) {
        result.push_back(AABB(blockX + cb.minX, blockY + cb.minY, blockZ + cb.minZ,
                              blockX + cb.maxX, blockY + cb.maxY, blockZ + cb.maxZ));
    }
    return result;
}

// ---------- 访问器 ----------
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
    alpha = std::clamp(alpha, 0.0f, 1.0f);
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
    isFlying = (mode == 1 || mode == 3);
    LOGI("Game mode set to %d (noClip=%s, flying=%s)", mode, noClip ? "true" : "false", isFlying ? "true" : "false");
}