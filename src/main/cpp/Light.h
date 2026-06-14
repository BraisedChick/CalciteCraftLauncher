#pragma once

#include <GLES3/gl3.h>
#include <cstdint>

class ChunkManager;

/// 光照系统管理：光照贴图纹理 + 昼夜循环 + 天空颜色 + 方块光传播
class Light {
public:
    static Light& getInstance();

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
    /// 当 BlockUpdate 到达时调用：重新计算受影响区域的方块光照
    void recalcBlockLight(ChunkManager* chunkMgr, int worldX, int worldY, int worldZ);

    /// 根据方块名称获取发光等级（0-15）
    static int getBlockEmission(const char* blockName);

private:
    Light() = default;

    GLuint lightmapTextureID = 0;
    long long worldDayTime = 6000;
    float skyR = 0.53f, skyG = 0.81f, skyB = 0.92f;
    void generateLightmapPixels(float skyBright, uint8_t* pixels);
};
