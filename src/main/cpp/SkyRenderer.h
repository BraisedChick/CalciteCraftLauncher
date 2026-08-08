#pragma once

#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <GLES3/gl3.h>
#include "TextureLoader.h"
#include "Light.h"

struct SkyRenderParams {
    float timeOfDay;          // 0~24000（已由 computeSkyParams 设为 normalizedTime*24000）
    float starBrightness;     // 0~1
    float normalizedTime;     // 0~1（已应用原版偏移和曲线）
    int moonPhase;            // 0~7
};

class SkyRenderer {
public:
    struct Vertex {
        float x, y, z;
        float u, v;
        Vertex(float x_, float y_, float z_, float u_, float v_)
                : x(x_), y(y_), z(z_), u(u_), v(v_) {}
    };
    static SkyRenderParams computeSkyParams(Light* light);
    static const char* getMoonPhasePath(int phase);

    static std::vector<float> getTopSkyVertices();
    static std::vector<float> getBottomSkyVertices();

    static std::vector<Vertex> getSunVertices();
    static std::vector<uint16_t> getSunIndices();
    static std::vector<Vertex> getMoonVertices();
    static std::vector<uint16_t> getMoonIndices();

    static std::vector<float> getStarVertices(int& outStarCount);
    static std::vector<uint16_t> getStarIndices(int starCount);

    static std::vector<float> getSunriseVertices();
    static std::vector<uint16_t> getSunriseIndices();

    // 天体旋转：接受 normalizedTime (0~1)
    static glm::mat4 getCelestialRotation(float normalizedTime);
    static float sunAngle(float timeOfDay); // 保留旧接口

    // 透明度计算
    static float getCelestialAlpha(bool isMoon = false);
    static float getStarBrightness(float timeOfDay);
};