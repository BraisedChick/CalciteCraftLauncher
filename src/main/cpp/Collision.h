#pragma once
#include <glm/glm.hpp>
#include <mutex>
#include <atomic>
#include <vector>

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
};

class Collision {
public:
    static Collision& getInstance() {
        static Collision instance;
        return instance;
    }

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

private:
    Collision();

    bool keyW = false;
    bool keyS = false;
    bool keyA = false;
    bool keyD = false;
    bool keyUp = false;
    bool keyDown = false;
    bool keySprint = false;
    bool jumpPressed = false;
    std::atomic<float> joystickDX{0.0f};
    std::atomic<float> joystickDY{0.0f};

    glm::vec3 position{-176.0f, 65.0f, -56.0f};
    glm::vec3 prevPosition{-176.0f, 65.0f, -56.0f};
    glm::vec3 velocity{0.0f};
    bool onGround = false;
    float accumulatedTime = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    ChunkManager* chunkManager = nullptr;
    mutable std::mutex mutex;

    int gameMode = 0;  // 0=survival, 1=creative, 2=adventure, 3=spectator
    bool noClip = false;

    static constexpr float TICK_DURATION = 1.0f / 20.0f;
    static constexpr float GRAVITY = 0.08f;
    static constexpr float JUMP_VELOCITY = 0.42f;
    static constexpr float PLAYER_WIDTH = 0.6f;
    static constexpr float PLAYER_HEIGHT = 1.8f;
    static constexpr float MOVE_SPEED = 0.22f;
    static constexpr float MOVE_ACCELERATION = 0.15f;
    static constexpr float SPRINT_MOVE_SPEED = 0.30f;
    static constexpr float SPRINT_MOVE_ACCELERATION = 0.20f;
    static constexpr float STEP_HEIGHT = 0.5f;  // 自动踏步最大高度

    void tick();
    AABB getPlayerAABB() const;
    std::vector<AABB> getBlockAABBs(int blockX, int blockY, int blockZ) const;
};
