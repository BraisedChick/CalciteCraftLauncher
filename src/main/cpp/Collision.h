#pragma once
#include <glm/glm.hpp>
#include <mutex>
#include <atomic>
#include <vector>
#include <optional>

class ChunkManager;

struct AABB {
    double minX, minY, minZ;
    double maxX, maxY, maxZ;

    AABB() : minX(0), minY(0), minZ(0), maxX(0), maxY(0), maxZ(0) {}
    AABB(double minX_, double minY_, double minZ_, double maxX_, double maxY_, double maxZ_)
            : minX(minX_), minY(minY_), minZ(minZ_), maxX(maxX_), maxY(maxY_), maxZ(maxZ_) {}

    AABB offset(double dx, double dy, double dz) const {
        return {minX + dx, minY + dy, minZ + dz,
                maxX + dx, maxY + dy, maxZ + dz};
    }

    bool intersects(const AABB& other) const {
        return minX < other.maxX && maxX > other.minX &&
               minY < other.maxY && maxY > other.minY &&
               minZ < other.maxZ && maxZ > other.minZ;
    }

    AABB inflate(double d) const {
        return {minX - d, minY - d, minZ - d,
                maxX + d, maxY + d, maxZ + d};
    }

    // 获取中心
    glm::dvec3 getCenter() const {
        return glm::dvec3((minX + maxX) * 0.5, (minY + maxY) * 0.5, (minZ + maxZ) * 0.5);
    }

    // 获取半尺寸
    glm::dvec3 getHalfSize() const {
        return glm::dvec3((maxX - minX) * 0.5, (maxY - minY) * 0.5, (maxZ - minZ) * 0.5);
    }

    // 平移
    void translate(const glm::dvec3& t) {
        minX += t.x; minY += t.y; minZ += t.z;
        maxX += t.x; maxY += t.y; maxZ += t.z;
    }

    // AABB + dvec3 偏移（返回新AABB）
    AABB operator+(const glm::dvec3& offset) const {
        return {minX + offset.x, minY + offset.y, minZ + offset.z,
                maxX + offset.x, maxY + offset.y, maxZ + offset.z};
    }

    // AABB - dvec3（反向偏移）（返回新AABB）
    AABB operator-(const glm::dvec3& offset) const {
        return {minX - offset.x, minY - offset.y, minZ - offset.z,
                maxX - offset.x, maxY - offset.y, maxZ - offset.z};
    }
};

class Collision {
public:
    Collision() = default;

    void setKeyState(int key, bool pressed);
    void setJoystickInput(float dx, float dy);
    void update(float deltaTime, float camPitch, float camYaw, glm::dvec3* outPosition = nullptr, bool* outOnGround = nullptr);
    void setGameMode(int mode);
    int getGameMode() const { return gameMode; }
    glm::vec3 getPosition() const;
    glm::vec3 getVelocity() const;
    glm::vec3 getSmoothPosition() const;
    bool isOnGround() const;
    void setPosition(double x, double y, double z);
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
    glm::dvec3 position{0.0, 60.0, 0.0};
    glm::dvec3 prevPosition{0.0, 60.0, 0.0};
    glm::dvec3 velocity{0.0};
    glm::dvec3 stuckSpeedMultiplier{1.0, 1.0, 1.0};
    bool onGround = false;
    bool horizontalCollision = false;
    double accumulatedTime = 0.0;
    double pitch = 0.0, yaw = 0.0;

    ChunkManager* chunkManager = nullptr;
    mutable std::mutex mutex;

    int gameMode = 0;
    bool noClip = false;
    bool isFlying = false;
    int playerEntityId = -1;

    // 常量
    static constexpr double TICK_DURATION = 1.0 / 20.0;
    static constexpr double GRAVITY = 0.08;
    static constexpr double JUMP_VELOCITY = 0.42;
    static constexpr double PLAYER_WIDTH = 0.6;
    static constexpr double PLAYER_HEIGHT = 1.8;
    static constexpr double MOVE_SPEED = 0.22;
    static constexpr double MOVE_ACCELERATION = 0.15;
    static constexpr double SPRINT_MOVE_SPEED = 0.30;
    static constexpr double SPRINT_MOVE_ACCELERATION = 0.20;
    static constexpr double STEP_HEIGHT = 0.6;
    static constexpr double EPSILON = 1e-7;

    void tick();
    void movePlayer();
    void applyMovement();
    glm::dvec3 collideBoundingBox(const AABB& aabb, const glm::dvec3& movement) const;
    void collideOneAxis(AABB& aabb, glm::dvec3& movement, int axis, const std::vector<AABB>& colliders) const;
    void checkInsideBlocks();

    AABB getPlayerAABB() const;
    // 注意：这里不再重复声明 getBlockAABBs

    bool isSwimming() const { return false; } // 暂时留空
    glm::dvec3 getInputVector() const;
    void applyInputs(double strength);
};