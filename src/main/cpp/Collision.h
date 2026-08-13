#pragma once
#include <glm/glm.hpp>
#include <mutex>
#include <atomic>
#include <vector>
#include <optional>

class ChunkManager;

struct AABB {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;

    AABB() : minX(0), minY(0), minZ(0), maxX(0), maxY(0), maxZ(0) {}
    AABB(float minX_, float minY_, float minZ_, float maxX_, float maxY_, float maxZ_)
            : minX(minX_), minY(minY_), minZ(minZ_), maxX(maxX_), maxY(maxY_), maxZ(maxZ_) {}

    AABB offset(float dx, float dy, float dz) const {
        return {minX + dx, minY + dy, minZ + dz,
                maxX + dx, maxY + dy, maxZ + dz};
    }

    bool intersects(const AABB& other) const {
        return minX < other.maxX && maxX > other.minX &&
               minY < other.maxY && maxY > other.minY &&
               minZ < other.maxZ && maxZ > other.minZ;
    }

    AABB inflate(float d) const {
        return {minX - d, minY - d, minZ - d,
                maxX + d, maxY + d, maxZ + d};
    }

    // 新增：获取中心
    glm::vec3 getCenter() const {
        return glm::vec3((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f);
    }

    // 新增：获取半尺寸
    glm::vec3 getHalfSize() const {
        return glm::vec3((maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f);
    }

    // 新增：平移（直接修改）
    void translate(const glm::vec3& t) {
        minX += t.x; minY += t.y; minZ += t.z;
        maxX += t.x; maxY += t.y; maxZ += t.z;
    }

    // 新增：AABB + vec3 偏移（返回新AABB）
    AABB operator+(const glm::vec3& offset) const {
        return {minX + offset.x, minY + offset.y, minZ + offset.z,
                maxX + offset.x, maxY + offset.y, maxZ + offset.z};
    }

    // 新增：AABB - vec3（反向偏移）
    AABB operator-(const glm::vec3& offset) const {
        return {minX - offset.x, minY - offset.y, minZ - offset.z,
                maxX - offset.x, maxY - offset.y, maxZ - offset.z};
    }
};

class Collision {
public:
    Collision() = default;

    void setKeyState(int key, bool pressed);
    void setJoystickInput(float dx, float dy);
    void update(float deltaTime, float camPitch, float camYaw, glm::vec3* outPosition = nullptr, bool* outOnGround = nullptr);
    void setGameMode(int mode);
    int getGameMode() const { return gameMode; }
    glm::vec3 getPosition() const;
    glm::vec3 getVelocity() const;
    glm::vec3 getSmoothPosition() const;
    bool isOnGround() const;
    void setPosition(float x, float y, float z);
    void setChunkManager(ChunkManager* mgr);
    bool hasChunkManager() const;
    void resetMovement();
    void setPlayerEntityId(int id) { playerEntityId = id; }

    // 唯一公开的碰撞箱获取函数（供 Raycast 等使用）
    std::vector<AABB> getBlockAABBs(int blockX, int blockY, int blockZ) const;

private:
    // 输入状态
    bool keyW = false, keyS = false, keyA = false, keyD = false;
    bool keyUp = false, keyDown = false;
    bool keySprint = false;
    bool jumpPressed = false;
    std::atomic<float> joystickDX{0.0f};
    std::atomic<float> joystickDY{0.0f};

    // 物理状态
    glm::vec3 position{0.0f, 60.0f, 0.0f};
    glm::vec3 prevPosition{0.0f, 60.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 stuckSpeedMultiplier{1.0f, 1.0f, 1.0f};
    bool onGround = false;
    bool horizontalCollision = false;
    float accumulatedTime = 0.0f;
    float pitch = 0.0f, yaw = 0.0f;

    ChunkManager* chunkManager = nullptr;
    mutable std::mutex mutex;

    int gameMode = 0;
    bool noClip = false;
    bool isFlying = false;
    int playerEntityId = -1;

    // 常量
    static constexpr float TICK_DURATION = 1.0f / 20.0f;
    static constexpr float GRAVITY = 0.08f;
    static constexpr float JUMP_VELOCITY = 0.42f;
    static constexpr float PLAYER_WIDTH = 0.6f;
    static constexpr float PLAYER_HEIGHT = 1.8f;
    static constexpr float MOVE_SPEED = 0.22f;
    static constexpr float MOVE_ACCELERATION = 0.15f;
    static constexpr float SPRINT_MOVE_SPEED = 0.30f;
    static constexpr float SPRINT_MOVE_ACCELERATION = 0.20f;
    static constexpr float STEP_HEIGHT = 0.6f;
    static constexpr float EPSILON = 1e-7f;

    void tick();
    void movePlayer();
    void applyMovement();
    glm::vec3 collideBoundingBox(const AABB& aabb, const glm::vec3& movement) const;
    void collideOneAxis(AABB& aabb, glm::vec3& movement, int axis, const std::vector<AABB>& colliders) const;
    void checkInsideBlocks();

    AABB getPlayerAABB() const;
    // 注意：这里不再重复声明 getBlockAABBs

    bool isSwimming() const { return false; } // 暂时留空
    glm::vec3 getInputVector() const;
    void applyInputs(float strength);
};