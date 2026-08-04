#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

// 游戏内天空渲染（天空圆盘 + 太阳 + 月亮 + 星星）
// 纯逻辑层：几何生成，不接触任何图形 API。
// GLRenderer 拿到几何数据后负责着色器、VAO 与绘制。
class SkyRenderer {
public:
    // ===== 天空圆盘（TRIANGLE_FAN）=====
    // 顶点格式：x, y, z（3 floats）
    // 上半圆盘（y = +16，半径 512）
    static const std::vector<float>& topSkyVertices();
    // 下半圆盘（y = -16，半径 512）
    static const std::vector<float>& bottomSkyVertices();

    // ===== 太阳（QUAD，4 顶点）=====
    // 顶点格式：x, y, z, u, v（5 floats）
    // 位于 y=100，大小 60×60
    static const std::vector<float>& sunVertices();
    static const std::vector<uint16_t>& sunIndices();

    // ===== 月亮（QUAD，4 顶点）=====
    // 位于 y=-100，大小 40×40
    static const std::vector<float>& moonVertices();
    static const std::vector<uint16_t>& moonIndices();

    // ===== 星星（1500 个 QUAD）=====
    // 顶点格式：x, y, z（3 floats），每个星星 4 个顶点 + 6 个索引
    static const std::vector<float>& starVertices();
    static const std::vector<uint16_t>& starIndices();

    // ===== 日出/日落渐变（TRIANGLE_FAN）=====
    // 顶点格式：x, y, z（3 floats）
    static const std::vector<float>& sunriseVertices();
    static const std::vector<uint16_t>& sunriseIndices();

    // ===== 天体旋转矩阵 =====
    // 根据昼夜时间（0-24000）计算太阳/月亮/星星的旋转角度
    // 返回绕 X 轴的旋转矩阵（太阳从东方升起）
    static glm::mat4 celestialRotation(float timeOfDay);

    // 获取太阳角度（弧度，0=日出，π=日落）
    static float sunAngle(float timeOfDay);
};
