#pragma once
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>

class ChunkManager;
class NetworkManager;
class GLRenderer;

class ClientEngine {
public:
    ClientEngine();
    ~ClientEngine();

    static ClientEngine* getInstance() { return instance; }

    bool start(const std::string& host, int port, const std::string& username);

    // 获取 ChunkManager 引用
    ChunkManager* getChunkManager() { return chunkManager.get(); }

    // 设置渲染器引用
    void setRenderer(GLRenderer* renderer) { glRenderer = renderer; }

    // 获取玩家位置
    double getPlayerX() const { return playerX; }
    double getPlayerY() const { return playerY; }
    double getPlayerZ() const { return playerZ; }
    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    bool hasPlayerPosition() const { return hasPosition; }

    // 发送数据包（线程安全）
    bool sendPacket(const std::vector<uint8_t>& data);
    bool isConnected() const;

    // 发送玩家移动更新到服务器
    void sendPlayerMovement(double x, double y, double z, float yaw, float pitch, bool onGround);

    // 发送手持物品槽位切换（0-8）
    void sendHeldItemChange(int slot);

    // 断开连接（线程安全）
    void disconnect();

    // 玩家生命/饥饿值
    float getHealth() const { return health; }
    int getFood() const { return food; }
    float getFoodSaturation() const { return foodSaturation; }
    int getGameMode() const { return gameMode; }

private:
    void handlePlayPacket(int packetId,
                         const std::vector<uint8_t>& data, size_t startPos);
    void parseChunkDataPacket(const std::vector<uint8_t>& data, size_t startPos);
    size_t calculateNBTSize(const std::vector<uint8_t>& data, size_t startPos);

    std::unique_ptr<ChunkManager> chunkManager;
    std::unique_ptr<NetworkManager> net;
    mutable std::mutex netMutex;
    GLRenderer* glRenderer = nullptr;

    static ClientEngine* instance;

    // 玩家位置
    double playerX = 0.0;
    double playerY = 65.0;  // 默认地表高度
    double playerZ = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool hasPosition = false;  // 是否收到过玩家位置

    // 维度信息 (Minecraft 1.18+)
    int dimensionMinY = -64;     // 世界最小 Y 坐标（标准 Overworld 为 -64）
    int dimensionHeight = 384;   // 世界高度（标准 Overworld 为 384，范围 -64 到 320）

    // 移动包跟踪（避免重复发送）
    struct MovementState {
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
        bool onGround = false;
        bool initialized = false;
    };
    MovementState lastSent;
    int moveTickCounter = 0;
    std::atomic<bool> movementEnabled{false};

    // 玩家生命/饥饿值
    float health = 20.0f;
    int food = 20;
    float foodSaturation = 5.0f;

    // 游戏模式
    int gameMode = 0;
};

