#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <unordered_map>

class AESEncrypter;
class ClientEngine;

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    // === Socket I/O ===
    bool connect(const std::string& host, int port);
    void disconnect();
    bool sendRawPacket(const std::vector<uint8_t>& fullPacketData);
    std::vector<uint8_t> receivePacket();
    bool isConnected() const;

    // === AES 加密 ===
    void setEncrypter(AESEncrypter* encrypter);
    bool isEncrypted() const;

    // === 引擎引用（handlers 通过此指针更新游戏状态） ===
    void setEngine(ClientEngine* engine) { m_engine = engine; }

    // === 主入口（阻塞直到断开连接） ===
    bool run(const std::string& host, int port, const std::string& username);

    // === 线程安全发包 ===
    bool sendPacket(const std::vector<uint8_t>& data);
    bool isCompressionEnabled() const;

    // === PLAY 状态主循环（阻塞直到断开连接） ===
    void startPlayLoop();

    // === Handler 注册表（由 ClientEngine 在登录完成后调用） ===
    void registerHandlers();

    // === 区块数据入队（由 WorldHandler 从 ClientEngine 调用） ===
    void enqueueChunkData(std::vector<uint8_t> rawData);

private:
    // 加密包接收
    std::vector<uint8_t> receiveEncryptedPacket();

    // === Handler 注册表与分发 ===
    using PacketHandler = void (NetworkManager::*)(int packetId, const std::vector<uint8_t>& data, size_t startPos);
    std::unordered_map<int, PacketHandler> m_packetHandlers;
    void handlePlayPacket(int packetId, const std::vector<uint8_t>& data, size_t startPos);

    // 业务域 handler
    void handleLogin(int packetId, const std::vector<uint8_t>& data, size_t startPos);
    void handleWorld(int packetId, const std::vector<uint8_t>& data, size_t startPos);
    void handleEntity(int packetId, const std::vector<uint8_t>& data, size_t startPos);
    void handleInventory(int packetId, const std::vector<uint8_t>& data, size_t startPos);
    void handleChat(int packetId, const std::vector<uint8_t>& data, size_t startPos);
    void handlePlayerStatus(int packetId, const std::vector<uint8_t>& data, size_t startPos);

    // === 数据包队列与处理线程 ===
    struct PacketTask {
        int packetId;
        std::vector<uint8_t> data;
        size_t startPos;
    };

    // 紧急队列（延迟敏感：位置、方块更新、生命等）
    std::queue<PacketTask> urgentQueue;
    std::mutex urgentQueueMutex;
    std::condition_variable urgentCV;
    std::thread urgentProcessor;
    std::atomic<bool> urgentProcessorRunning{false};

    // 普通队列
    std::queue<PacketTask> normalQueue;
    std::mutex normalQueueMutex;
    std::condition_variable normalCV;
    std::thread normalProcessor;
    std::atomic<bool> normalProcessorRunning{false};

    // 区块数据队列
    struct ChunkLoadTask {
        std::vector<uint8_t> rawData;
    };
    std::queue<ChunkLoadTask> chunkQueue;
    std::mutex chunkQueueMutex;
    std::condition_variable chunkCV;
    std::thread chunkWorker;
    std::atomic<bool> chunkWorkerRunning{false};

    void urgentProcessorFunc();
    void normalProcessorFunc();
    void parseChunkDataPacket(const std::vector<uint8_t>& data, size_t startPos);
    size_t calculateNBTSize(const std::vector<uint8_t>& data, size_t startPos);
    void chunkWorkerFunc();

    // === 网络底层 ===
    int sock = -1;
    bool connected = false;
    AESEncrypter* encrypter = nullptr;

    // 引擎引用（用于 handlers 更新游戏状态）
    ClientEngine* m_engine = nullptr;
};
