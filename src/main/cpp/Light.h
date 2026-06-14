#pragma once

#include <GLES3/gl3.h>
#include <cstdint>

/// 光照系统管理：光照贴图纹理 + 昼夜循环 + 天空颜色
class Light {
public:
    static Light& getInstance();

    // ===== 光照贴图纹理 =====
    /// 创建 16×16 光照贴图 GL 纹理（初始化时调用一次）
    void createLightmapTexture();

    /// 每帧调用：根据昼夜更新光照贴图像素和天空颜色
    void update();

    GLuint getLightmapTextureID() const { return lightmapTextureID; }

    // ===== 昼夜循环 =====
    /// 设置世界时间（DayTime 0-24000，来自 SetTimePacket）
    void setWorldDayTime(long long dayTime) { worldDayTime = dayTime; }
    long long getWorldDayTime() const { return worldDayTime; }

    /// 天空暗度因子：0.0=白天，1.0=夜晚
    float getSkyDarken() const;

    // ===== 天空/雾效颜色（由 update() 计算）=====
    float getSkyColorR() const { return skyR; }
    float getSkyColorG() const { return skyG; }
    float getSkyColorB() const { return skyB; }

private:
    Light() = default;

    GLuint lightmapTextureID = 0;
    long long worldDayTime = 6000;  // 默认正午

    // 当前天空颜色（由 update() 计算）
    float skyR = 0.53f, skyG = 0.81f, skyB = 0.92f;

    /// 生成光照贴图像素数据
    void generateLightmapPixels(float skyBright, uint8_t* pixels);
};
