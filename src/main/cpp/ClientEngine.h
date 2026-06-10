#pragma once
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <queue>
#include <thread>
#include <condition_variable>
#include <map>

class ChunkManager;
class NetworkManager;
class GLRenderer;
class AESEncrypter;

class ClientEngine {
public:
    ClientEngine();
    ~ClientEngine();

    static ClientEngine* getInstance() { return instance; }

    bool start(const std::string& host, int port, const std::string& username);

    // 设置正版认证信息
    void setAuthInfo(const std::string& accessToken, const std::string& uuid, const std::string& tokenType);
    bool isPremium() const { return g_isPremium; }

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

    // 发送方块放置（UseItemOn 包）
    // blockX/Y/Z 是被点击的目标方块坐标，face 是击中面（0-5，对应 FaceDir）
    void sendBlockPlacement(int blockX, int blockY, int blockZ, int face, int hand = 0);

    // 发送方块破坏（PlayerAction 包）
    // blockX/Y/Z 是目标方块坐标，face 是破坏面（0-5）
    void sendBlockBreakStart(int blockX, int blockY, int blockZ, int face);
    void sendBlockBreakFinish(int blockX, int blockY, int blockZ, int face);
    void sendBlockBreakAbort(int blockX, int blockY, int blockZ, int face);

    // 发送重生请求
    void sendRespawn();

    // 断开连接（线程安全）
    void disconnect();

    // 玩家生命/饥饿值
    float getHealth() const { return health; }
    int getFood() const { return food; }
    float getFoodSaturation() const { return foodSaturation; }
    int getGameMode() const { return gameMode; }

    // 死亡消息（来自服务端 CombatKill 包）
    std::string getDeathMessage() const { return deathMessage; }
    void clearDeathMessage() { deathMessage.clear(); }

    // 加载语言文件（如 zh_cn.json）
    void loadLanguage(const std::string& json);

private:
    // 紧急数据包队列（延迟敏感：位置、方块更新、生命等）
    struct PacketTask {
        int packetId;
        std::vector<uint8_t> data;
        size_t startPos;
    };
    std::queue<PacketTask> urgentQueue;
    std::mutex urgentQueueMutex;
    std::condition_variable urgentCV;
    std::thread urgentProcessor;
    std::atomic<bool> urgentProcessorRunning{false};

    // 普通数据包队列（登录、物品、聊天等）
    std::queue<PacketTask> normalQueue;
    std::mutex normalQueueMutex;
    std::condition_variable normalCV;
    std::thread normalProcessor;
    std::atomic<bool> normalProcessorRunning{false};

    struct ChunkLoadTask {
        std::vector<uint8_t> rawData;  // 完整原始包数据（从 VarInt chunk packet ID 之后开始）
    };
    std::queue<ChunkLoadTask> chunkQueue;
    std::mutex chunkQueueMutex;
    std::condition_variable chunkCV;
    std::thread chunkWorker;
    std::atomic<bool> chunkWorkerRunning{false};

    void handlePlayPacket(int packetId,
                         const std::vector<uint8_t>& data, size_t startPos);
    void urgentProcessorFunc();
    void normalProcessorFunc();
    void parseChunkDataPacket(const std::vector<uint8_t>& data, size_t startPos);
    size_t calculateNBTSize(const std::vector<uint8_t>& data, size_t startPos);
    void chunkWorkerFunc();

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
    std::chrono::steady_clock::time_point lastMoveSendTime;
    std::atomic<bool> movementEnabled{false};

    // 玩家生命/饥饿值
    float health = 20.0f;
    int food = 20;
    float foodSaturation = 5.0f;

    // 游戏模式
    int gameMode = 0;

    // 死亡消息（服务端 CombatKill 包提供）
    std::string deathMessage;

    // 语言翻译表（从 zh_cn.json 加载）
    std::map<std::string, std::string> translations;

    // 正版认证信息
    std::string g_accessToken;
    std::string g_playerUuid;
    std::string g_tokenType;
    bool g_isPremium = false;

    // AES 加密器（在线模式服务器启用加密后使用）
    std::unique_ptr<AESEncrypter> aesEncrypter;
};

