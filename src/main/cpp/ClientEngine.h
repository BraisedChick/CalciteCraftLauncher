#pragma once
#include <string>
#include <memory>
#include <vector>
#include <cstdint>

class ChunkManager;
class NetworkManager;
class GLRenderer;

class ClientEngine {
public:
    ClientEngine();
    ~ClientEngine();

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

private:
    void handlePlayPacket(NetworkManager& net, int packetId,
                         const std::vector<uint8_t>& data, size_t startPos);
    void parseChunkDataPacket(const std::vector<uint8_t>& data, size_t startPos);
    size_t calculateNBTSize(const std::vector<uint8_t>& data, size_t startPos);

    std::unique_ptr<ChunkManager> chunkManager;
    GLRenderer* glRenderer = nullptr;
    
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
};