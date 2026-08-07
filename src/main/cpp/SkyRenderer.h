#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <GLES3/gl3.h>
#include "TextureLoader.h"

class SkyRenderer {
public:
    struct Vertex {
        float x, y, z;
        float u, v;
        Vertex(float x_, float y_, float z_, float u_, float v_)
                : x(x_), y(y_), z(z_), u(u_), v(v_) {}
    };

    // 获取天空圆盘顶点（大小固定）
    static std::vector<float> getTopSkyVertices();
    static std::vector<float> getBottomSkyVertices();

    // 太阳/月亮（固定大小）
    static std::vector<Vertex> getSunVertices();
    static std::vector<uint16_t> getSunIndices();
    static std::vector<Vertex> getMoonVertices();
    static std::vector<uint16_t> getMoonIndices();

    // ★★★ 星星新接口：返回顶点，并通过引用参数返回实际星星数量 ★★★
    static std::vector<float> getStarVertices(int& outStarCount);
    // 索引生成接受星星数量
    static std::vector<uint16_t> getStarIndices(int starCount);

    // 日出渐变
    static std::vector<float> getSunriseVertices();
    static std::vector<uint16_t> getSunriseIndices();

    // 天体旋转 & 纹理加载
    static glm::mat4 getCelestialRotation(float timeOfDay);
    static float sunAngle(float timeOfDay);
    static GLuint loadSunTexture();
    static GLuint loadMoonTexture();
    static void clearTextures();

    // Fallback 纹理（已移除）

    // 动画计算
    static float getCelestialAlpha(float timeOfDay, bool isMoon = false);
    static glm::vec3 getSkyColor(float timeOfDay);
    static float getStarBrightness(float timeOfDay);
};