#pragma once

#include <GLES3/gl3.h>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>

class ChunkManager;

/// 光照系统管理：光照贴图纹理 + 昼夜循环 + 天空颜色 + 方块光传播
class Light {
public:
    static Light& getInstance();
    ~Light();

    // ===== 光照贴图纹理 =====
    void createLightmapTexture();
    void update();
    GLuint getLightmapTextureID() const { return lightmapTextureID; }

    // ===== 昼夜循环 =====
    void setWorldDayTime(long long dayTime) { worldDayTime = dayTime; }
    long long getWorldDayTime() const { return worldDayTime; }
    float getSkyDarken() const;

    // ===== 天空/雾效颜色 =====
    float getSkyColorR() const { return skyR; }
    float getSkyColorG() const { return skyG; }
    float getSkyColorB() const { return skyB; }

    // ===== 客户端方块光传播（BFS）=====
    /// 异步入队：网络线程调用，不阻塞
    void queueBlockLightRecalc(int worldX, int worldY, int worldZ);

    /// 检查是否有已完成的光照重算，返回位置用于标记 chunk
    bool pollCompletedLightRecalc(int* outX, int* outY, int* outZ);

    /// 同步执行光照重算（内部使用）
    void recalcBlockLight(ChunkManager* chunkMgr, int worldX, int worldY, int worldZ);

    /// 根据方块名称获取发光等级（0-15）
    static int getBlockEmission(const char* blockName);

    /// 设置 ChunkManager 引用（后台线程用）
    void setChunkManager(ChunkManager* cm) { chunkManagerRef = cm; }

private:
    Light();
    void startWorkerThread();
    void workerLoop();

    GLuint lightmapTextureID = 0;
    long long worldDayTime = 6000;
    float skyR = 0.53f, skyG = 0.81f, skyB = 0.92f;
    void generateLightmapPixels(float skyBright, uint8_t* pixels);

    // 异步光照重算
    ChunkManager* chunkManagerRef = nullptr;
    std::thread workerThread;
    std::atomic<bool> workerRunning{false};

    // 输入队列（网络线程 → 工作线程）
    std::mutex inputMutex;
    std::condition_variable inputCV;
    int pendingLightX = 0, pendingLightY = 0, pendingLightZ = 0;
    bool hasInput = false;

    // 输出队列（工作线程 → 渲染线程）
    std::mutex outputMutex;
    int completedX = 0, completedY = 0, completedZ = 0;
    bool hasOutput = false;
};
