#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <GLES3/gl3.h>
#include "TextureLoader.h"

// 游戏内天空渲染（天空圆盘 + 太阳 + 月亮 + 星星）
// 纯逻辑层：几何生成、纹理加载、动画计算，不接触任何图形 API。
// GLRenderer 拿到顶点数据和纹理 ID 后负责着色器、VAO 与绘制。
class SkyRenderer {
public:
    // ===== 顶点数据结构 =====
    struct Vertex {
        float x, y, z;     // 位置
        float u, v;        // 纹理坐标（仅太阳/月亮使用）

        Vertex(float px, float py, float pz, float pu = 0, float pv = 0)
            : x(px), y(py), z(pz), u(pu), v(pv) {}
    };

    // ===== 天空圆盘（TRIANGLE_FAN）=====
    // 顶点格式：x, y, z（3 floats）
    // 上半圆盘（y = +16，半径 512）
    static std::vector<float> getTopSkyVertices();
    // 下半圆盘（y = -16，半径 512）
    static std::vector<float> getBottomSkyVertices();

    // ===== 太阳（QUAD，4 顶点）=====
    // 包含位置和纹理坐标
    static std::vector<Vertex> getSunVertices();
    static std::vector<uint16_t> getSunIndices();

    // ===== 月亮（QUAD，4 顶点）=====
    // 包含位置和纹理坐标
    static std::vector<Vertex> getMoonVertices();
    static std::vector<uint16_t> getMoonIndices();

    // ===== 星星（1500 个 QUAD）=====
    // 顶点格式：x, y, z（3 floats）
    static std::vector<float> getStarVertices();
    static std::vector<uint16_t> getStarIndices();

    // ===== 日出/日落渐变（TRIANGLE_FAN）=====
    // 顶点格式：x, y, z（3 floats）
    static std::vector<float> getSunriseVertices();
    static std::vector<uint16_t> getSunriseIndices();

    // ===== 天体旋转矩阵 =====
    // 根据昼夜时间（0-24000）计算太阳/月亮/星星的旋转角度
    // 返回绕 X 轴的旋转矩阵（太阳从东方升起）
    static glm::mat4 getCelestialRotation(float timeOfDay);

    // 计算太阳角度（弧度）- 内部使用
    static float sunAngle(float timeOfDay);

    // ===== 纹理管理 =====
    // 加载太阳纹理（从 ResourcepackManager）
    static GLuint loadSunTexture();
    // 加载月亮纹理（从 ResourcepackManager）
    static GLuint loadMoonTexture();
    // 清理纹理资源
    static void clearTextures();

    // ===== 内部方法 =====
    // 创建 fallback 纹理
    static TextureData createFallbackSunTexture();
    static TextureData createFallbackMoonTexture();

    // ===== 动画计算 =====
    // 获取太阳/月亮的透明度（0-1）
    static float getCelestialAlpha(float timeOfDay, bool isMoon = false);
    // 获取天空颜色
    static glm::vec3 getSkyColor(float timeOfDay);
    // 获取星星亮度
    static float getStarBrightness(float timeOfDay);
};
