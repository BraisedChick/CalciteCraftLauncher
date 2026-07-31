#pragma once
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <map>
#include <chrono>
#include <jni.h>

class ChunkManager;
class NetworkManager;
class GLRenderer;
class ChunkMeshScheduler;
class ClientEngine;
class AESEncrypter;
class PlayerInventory;
class EntityManager;
class Collision;
class Light;
class Raycast;

// 会话引擎：一次服务器连接的完整生命周期
// 生命周期：连接服务器 → 断开连接
class GameEngine {
public:
    GameEngine(ClientEngine* client);
    ~GameEngine();

    // 反向引用：访问全局 ClientEngine（获取渲染器等）
    ClientEngine* getClient() { return m_client; }

    // ===== 连接与主循环 =====
    bool start(const std::string& host, int port);
    void disconnect();

    // ===== 网络发包（线程安全） =====
    bool sendPacket(const std::vector<uint8_t>& data);
    bool isConnected() const;

    // ===== 发送玩家移动更新到服务器 =====
    void sendPlayerMovement(double x, double y, double z, float yaw, float pitch, bool onGround);

    // ===== 发送手持物品槽位切换（0-8） =====
    void sendHeldItemChange(int slot);

    // ===== 发送方块放置（UseItemOn 包） =====
    void sendBlockPlacement(int blockX, int blockY, int blockZ, int face, int hand = 0);

    // ===== 发送使用手持物品（UseItem 包，如吃东西） =====
    void sendUseItem(int hand = 0);

    // ===== 发送方块破坏（PlayerAction 包） =====
    void sendBlockBreakStart(int blockX, int blockY, int blockZ, int face);
    void sendBlockBreakFinish(int blockX, int blockY, int blockZ, int face);
    void sendBlockBreakAbort(int blockX, int blockY, int blockZ, int face);

    // ===== 发送攻击实体包 =====
    void sendEntityAttack(int entityId);

    // ===== 发送重生请求 =====
    void sendRespawn();

    // ===== 发送聊天消息 =====
    void sendChatMessage(const std::string& message);

    // ===== 发送背包点击（左键拿取/放下） =====
    void sendContainerClick(int slotNum, int button, int containerId = -1);

    // ===== 发送容器关闭包 =====
    void sendContainerClose();

    // ===== 发送背包拖拽（Quick Craft） =====
    void sendContainerQuickCraft(int phase, int slotNum, int button);

    // ===== 获取游戏会话服务 =====
    ChunkManager* getChunkManager() { return chunkManager.get(); }
    PlayerInventory* getInventory() { return m_inventory.get(); }
    EntityManager* getEntityManager() { return m_entityManager.get(); }
    NetworkManager* getNetworkManager() { return net.get(); }
    Collision* getCollision() { return m_collision.get(); }
    Light* getLight() { return m_light.get(); }
    Raycast* getRaycast() const { return m_raycast.get(); }

    // ===== 获取渲染器（通过 ClientEngine 转发） =====
    GLRenderer* getRenderer();

    // ===== 获取区块网格调度器（通过 ClientEngine 转发，图形 API 无关） =====
    ChunkMeshScheduler* getMeshScheduler();

    // ===== 玩家位置 =====
    double getPlayerX() const { return playerX; }
    double getPlayerY() const { return playerY; }
    double getPlayerZ() const { return playerZ; }
    float getYaw() const { return yaw; }
    float getPitch() const { return pitch; }
    bool hasPlayerPosition() const { return hasPosition; }

    // ===== 玩家生命/饥饿值 =====
    float getHealth() const { return health; }
    int getFood() const { return food; }
    float getFoodSaturation() const { return foodSaturation; }
    int getGameMode() const { return gameMode; }

    // ===== 经验值 =====
    float getExperienceProgress() const { return experienceProgress; }
    int getExperienceLevel() const { return experienceLevel; }
    int getTotalExperience() const { return totalExperience; }

    // ===== 死亡消息 =====
    std::string getDeathMessage() const { return deathMessage; }
    void clearDeathMessage() { deathMessage.clear(); }

    // ===== 断开连接原因（连接失败/登录拒绝/被踢，空=正常断开） =====
    std::string getDisconnectReason() const { return disconnectReason; }
    // 是否已完成登录进入 play 阶段（区分原版标题：连接中失败=connect.failed，游戏中断开=disconnect.lost）
    bool isLoginCompleted() const { return loginCompleted; }

    // ===== 本地玩家实体 ID =====
    int getPlayerId() const { return playerId; }

    // ===== 维度信息 =====
    int getDimensionMinY() const { return dimensionMinY; }
    int getDimensionHeight() const { return dimensionHeight; }

    // 相机眼睛所在方块是否为实心不透明方块（供 BFS 遮挡剔除判断"相机嵌入地形"，
    // 对齐原版 LevelRenderer 旁观穿地兜底：此时关闭遮挡剔除只保留视锥剔除）
    bool isEyeInsideOpaqueBlock(double eyeX, double eyeY, double eyeZ) const;

    // ===== 世界时间 =====
    long long getWorldDayTime() const;
    float getSkyDarken() const;

    // ===== 聊天组件解析（翻译表向 ClientEngine 查询） =====
    std::string parseChatComponent(const std::string& rawJson) const;

    // ===== 正版认证（从 ClientEngine 复制） =====
    void setAuthInfo(const std::string& accessToken, const std::string& uuid, const std::string& tokenType);
    bool isPremium() const { return premium; }
    const std::string& getAccessToken() const { return accessToken; }
    const std::string& getPlayerUuid() const { return playerUuid; }
    const std::string& getTokenType() const { return tokenType; }

    // ===== 直接访问成员（handlers 需要） =====
    // 这些成员在网络 handler 中被直接读写

    std::unique_ptr<ChunkManager> chunkManager;
    std::unique_ptr<NetworkManager> net;
    std::unique_ptr<PlayerInventory> m_inventory;
    std::unique_ptr<EntityManager> m_entityManager;
    std::unique_ptr<Collision> m_collision;
    std::unique_ptr<Light> m_light;
    mutable std::mutex netMutex;

    // 玩家位置（handler 直接写入）
    double playerX = 0.0;
    double playerY = 65.0;
    double playerZ = 0.0;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool hasPosition = false;

    // 维度信息
    int dimensionMinY = -64;
    int dimensionHeight = 384;

    // 移动包跟踪
    struct MovementState {
        double x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0;
        bool onGround = false;
        bool initialized = false;
    };
    MovementState lastSent;
    std::chrono::steady_clock::time_point lastMoveSendTime;
    std::atomic<bool> movementEnabled{false};

    // 玩家状态
    float health = 20.0f;
    int food = 20;
    float foodSaturation = 5.0f;
    float experienceProgress = 0.0f;
    int experienceLevel = 0;
    int totalExperience = 0;
    int gameMode = 0;
    std::string deathMessage;
    // 断开原因（startPlayLoop 收到 Disconnect 包时由 NetworkManager 直接写入）
    std::string disconnectReason;
    // 登录完成标志（loginDone 后置 true，断开时决定 DisconnectedScreen 标题）
    bool loginCompleted = false;
    int playerId = -1;

private:
    ClientEngine* m_client;

    // 通过 JNI 调用 Java 层处理加密请求
    bool handleEncryptionRequest(
        const std::string& serverID,
        const std::vector<unsigned char>& publicKey,
        const std::vector<unsigned char>& verifyToken,
        std::vector<unsigned char>& sharedSecret,
        std::vector<unsigned char>& encryptedSecret,
        std::vector<unsigned char>& encryptedVerifyToken);

    // 语言翻译表已迁至 ClientEngine（客户端资源，与会话无关）

    // 正版认证信息
    std::string accessToken;
    std::string playerUuid;
    std::string tokenType;
    bool premium = false;

    // AES 加密器
    std::unique_ptr<AESEncrypter> aesEncrypter;

    // 射线检测器（注入 this，惰性访问 chunk/entity/collision/BlockRegistry）
    std::unique_ptr<Raycast> m_raycast;
};
