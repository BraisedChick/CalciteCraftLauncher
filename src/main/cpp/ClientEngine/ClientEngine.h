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
#include <unordered_map>
#include <jni.h>

class ChunkManager;
class NetworkManager;
class GLRenderer;
class AESEncrypter;
class PlayerInventory;
class EntityManager;
class EntityRenderer;

class ClientEngine {
    friend class NetworkManager;
public:
    ClientEngine();
    ~ClientEngine();

    static ClientEngine* getInstance() { return instance; }

    // 玩家用户名（JNI 层写入，连接时读取）
    static void setUsername(const std::string& name) { s_username = name; }
    static const std::string& getUsername() { return s_username; }

    // 暂存正版认证信息（JNI 层写入，连接回调读取）
    static void setPendingAuth(const std::string& accessToken, const std::string& uuid, const std::string& tokenType);
    static bool isPremiumPending();
    static const std::string& getPendingAccessToken();
    static const std::string& getPendingPlayerUuid();
    static const std::string& getPendingTokenType();

    bool start(const std::string& host, int port);

    // 设置正版认证信息
    void setAuthInfo(const std::string& accessToken, const std::string& uuid, const std::string& tokenType);
    bool isPremium() const { return premium; }
    const std::string& getAccessToken() const { return accessToken; }
    const std::string& getPlayerUuid() const { return playerUuid; }
    const std::string& getTokenType() const { return tokenType; }

    // 获取 ChunkManager 引用
    ChunkManager* getChunkManager() { return chunkManager.get(); }

    // 获取游戏会话服务
    PlayerInventory* getInventory() { return m_inventory.get(); }
    EntityManager* getEntityManager() { return m_entityManager.get(); }
    EntityRenderer* getEntityRenderer() { return m_entityRenderer.get(); }

    // 设置渲染器（转移所有权），并自动传播 ChunkManager 到渲染器/Collision/Light
    void setRenderer(std::unique_ptr<GLRenderer> renderer);

    // 释放渲染器所有权（用于断开连接时归还给 JNI 层暂存）
    std::unique_ptr<GLRenderer> releaseRenderer();

    // 获取渲染器裸指针（非拥有）
    GLRenderer* getRenderer() { return m_renderer.get(); }

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

    // 发送使用手持物品（UseItem 包，如吃东西）
    void sendUseItem(int hand = 0);

    // 发送方块破坏（PlayerAction 包）
    // blockX/Y/Z 是目标方块坐标，face 是破坏面（0-5）
    void sendBlockBreakStart(int blockX, int blockY, int blockZ, int face);
    void sendBlockBreakFinish(int blockX, int blockY, int blockZ, int face);
    void sendBlockBreakAbort(int blockX, int blockY, int blockZ, int face);

    // 发送攻击实体包（ATTACK via ServerboundInteractPacket）
    void sendEntityAttack(int entityId);

    // 发送重生请求
    void sendRespawn();

    // 发送聊天消息
    void sendChatMessage(const std::string& message);

    // 发送背包点击（左键拿取/放下）
    // slotNum: 槽位号（0-45 玩家背包），button: 0=左键 1=右键
    void sendContainerClick(int slotNum, int button, int containerId = -1);

    // 发送容器关闭包（关闭工作台等外部容器）
    void sendContainerClose();

    // 发送背包拖拽（Quick Craft）操作
    // 原版MC拖拽流程：按住鼠标拖过多个格子 → 松开时均分物品
    // phase: 0=开始, 1=拖过槽位, 2=结束（分发）
    // button: 0=左键均分, 1=右键每格1个
    void sendContainerQuickCraft(int phase, int slotNum, int button);

    // 断开连接（线程安全）
    void disconnect();

    // 玩家生命/饥饿值
    float getHealth() const { return health; }
    int getFood() const { return food; }
    float getFoodSaturation() const { return foodSaturation; }
    int getGameMode() const { return gameMode; }

    // 经验值
    float getExperienceProgress() const { return experienceProgress; }
    int getExperienceLevel() const { return experienceLevel; }
    int getTotalExperience() const { return totalExperience; }

    // 死亡消息（来自服务端 CombatKill 包）
    std::string getDeathMessage() const { return deathMessage; }
    void clearDeathMessage() { deathMessage.clear(); }

    // 本地玩家实体 ID
    int getPlayerId() const { return playerId; }

    // 世界时间（DayTime 0-24000，用于昼夜循环）
    long long getWorldDayTime() const;
    // 天空暗度因子：0.0=白天，1.0=夜晚
    float getSkyDarken() const;

    // 加载语言文件（如 zh_cn.json）
    void loadLanguage(const std::string& json);

private:
    // Chat Component JSON 解析（translate/with/text 多层结构）
    std::string parseChatComponent(const std::string& rawJson) const;

    // 通过 JNI 调用 Java 层处理加密请求（生成共享密钥 + SHA1 + Session Join + RSA）
    bool handleEncryptionRequest(
        const std::string& serverID,
        const std::vector<unsigned char>& publicKey,
        const std::vector<unsigned char>& verifyToken,
        std::vector<unsigned char>& sharedSecret,
        std::vector<unsigned char>& encryptedSecret,
        std::vector<unsigned char>& encryptedVerifyToken);

    std::unique_ptr<ChunkManager> chunkManager;
    std::unique_ptr<NetworkManager> net;
    std::unique_ptr<PlayerInventory> m_inventory;
    std::unique_ptr<EntityManager> m_entityManager;
    std::unique_ptr<EntityRenderer> m_entityRenderer;
    mutable std::mutex netMutex;

    std::unique_ptr<GLRenderer> m_renderer;

    static ClientEngine* instance;

    // 暂存的正版认证信息（连接前由 JNI 层写入）
    static std::string s_pendingAccessToken;
    static std::string s_pendingPlayerUuid;
    static std::string s_pendingTokenType;
    static std::string s_username;

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
    float experienceProgress = 0.0f;  // 0.0 ~ 1.0 当前等级进度
    int experienceLevel = 0;
    int totalExperience = 0;

    // 游戏模式
    int gameMode = 0;

    // 世界时间已移至 Light 单例管理

    // 死亡消息（服务端 CombatKill 包提供）
    std::string deathMessage;

    // 本地玩家实体 ID（LoginPacket 中获取，用于自身攻击跳过等）
    int playerId = -1;

    // 语言翻译表（从 zh_cn.json 加载）
    std::map<std::string, std::string> translations;

    // 正版认证信息
    std::string accessToken;
    std::string playerUuid;
    std::string tokenType;
    bool premium = false;

    // AES 加密器（在线模式服务器启用加密后使用）
    std::unique_ptr<AESEncrypter> aesEncrypter;
};

