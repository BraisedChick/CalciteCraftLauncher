#include "SkyRenderer.h"
#include <cmath>
#include <random>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <android/log.h>
#include <algorithm>  // for std::clamp

#define LOG_TAG "SkyRenderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// 工具函数：取正小数部分
namespace MathUtils {
    inline double frac(double x) {
        return x - std::floor(x);
    }
}

// ===== 天空圆盘 =====

static std::vector<float> buildSkyDisc(float y) {
    // 原版 MC：中心点 + 9 个环形顶点（每 45° 一个），半径 512
    std::vector<float> verts;
    float radius = 512.0f;
    float signedRadius = (y > 0 ? 1.0f : -1.0f) * radius;

    // 中心顶点
    verts.push_back(0.0f);
    verts.push_back(y);
    verts.push_back(0.0f);

    // 环形顶点（-180° 到 +180°，每 45°）
    for (int deg = -180; deg <= 180; deg += 45) {
        float rad = deg * (float)M_PI / 180.0f;
        verts.push_back(signedRadius * cosf(rad));
        verts.push_back(y);
        verts.push_back(radius * sinf(rad));
    }

    return verts;
}

std::vector<float> SkyRenderer::getTopSkyVertices() {
    return buildSkyDisc(16.0f);
}

std::vector<float> SkyRenderer::getBottomSkyVertices() {
    return buildSkyDisc(-16.0f);
}

// ===== 太阳 =====

std::vector<SkyRenderer::Vertex> SkyRenderer::getSunVertices() {
    // 60×60 quad at y=100
    return {
        Vertex(-30.0f, 100.0f, -30.0f, 0.0f, 0.0f),
        Vertex(30.0f, 100.0f, -30.0f, 1.0f, 0.0f),
        Vertex(30.0f, 100.0f, 30.0f, 1.0f, 1.0f),
        Vertex(-30.0f, 100.0f, 30.0f, 0.0f, 1.0f)
    };
}

std::vector<uint16_t> SkyRenderer::getSunIndices() {
    return { 0, 1, 2, 0, 2, 3 };
}

// ===== 月亮 =====

std::vector<SkyRenderer::Vertex> SkyRenderer::getMoonVertices() {
    // 40×40 quad at y=-100
    return {
        Vertex(-20.0f, -100.0f, 20.0f, 0.0f, 0.0f),
        Vertex(20.0f, -100.0f, 20.0f, 1.0f, 0.0f),
        Vertex(20.0f, -100.0f, -20.0f, 1.0f, 1.0f),
        Vertex(-20.0f, -100.0f, -20.0f, 0.0f, 1.0f)
    };
}

std::vector<uint16_t> SkyRenderer::getMoonIndices() {
    return { 0, 1, 2, 0, 2, 3 };
}

// ===== 星星 =====
std::vector<float> SkyRenderer::getStarVertices(int& outStarCount) {
    std::vector<float> verts;
    std::mt19937 rng(10842);
    float radius = 100.0f;
    int actualStarCount = 0;

    for (int j = 0; j < 1500; j++) {
        float f1 = (float)rng() / (float)rng.max() * 2.0f - 1.0f;
        float f2 = (float)rng() / (float)rng.max() * 2.0f - 1.0f;
        float f3 = (float)rng() / (float)rng.max() * 2.0f - 1.0f;
        float f5 = f1*f1 + f2*f2 + f3*f3;
        if (f5 <= 0.01f || f5 >= 1.0f) continue;

        // 归一化到球面
        glm::vec3 dir = glm::normalize(glm::vec3(f1, f2, f3)) * radius;

        // 星星大小
        float s = 0.15f + (float)rng() / (float)rng.max() * 0.1f;

        // 随机翻滚角
        float f6 = (float)rng() / (float)rng.max() * 2.0f * (float)M_PI;

        // 构建旋转矩阵（省略详细实现，与原来相同）
        // ... 原有旋转矩阵代码 ...
        glm::vec3 from = glm::normalize(-dir);
        glm::vec3 to   = glm::vec3(0.0f, 1.0f, 0.0f);
        float d = glm::dot(from, to);
        glm::quat q;
        if (d > -0.9999f) {
            glm::vec3 axis = glm::cross(from, to);
            q = glm::quat(1.0f + d, axis.x, axis.y, axis.z);
            q = glm::normalize(q);
        } else {
            glm::vec3 perp = (fabsf(from.x) < 0.9f)
                             ? glm::normalize(glm::cross(from, glm::vec3(1, 0, 0)))
                             : glm::normalize(glm::cross(from, glm::vec3(0, 0, 1)));
            q = glm::quat(0.0f, perp.x, perp.y, perp.z);
        }
        glm::mat3 rotMat(q);
        float cosR = cosf(-f6);
        float sinR = sinf(-f6);
        glm::mat3 zRot(
                cosR, -sinR, 0.0f,
                sinR,  cosR, 0.0f,
                0.0f,  0.0f, 1.0f
        );
        glm::mat3 finalRot = rotMat * zRot;

        // 4 个顶点（原版 MC 顺序）
        glm::vec3 v0 = finalRot * glm::vec3( s, -s, 0.0f) + dir;
        glm::vec3 v1 = finalRot * glm::vec3( s,  s, 0.0f) + dir;
        glm::vec3 v2 = finalRot * glm::vec3(-s,  s, 0.0f) + dir;
        glm::vec3 v3 = finalRot * glm::vec3(-s, -s, 0.0f) + dir;

        verts.push_back(v0.x); verts.push_back(v0.y); verts.push_back(v0.z);
        verts.push_back(v1.x); verts.push_back(v1.y); verts.push_back(v1.z);
        verts.push_back(v2.x); verts.push_back(v2.y); verts.push_back(v2.z);
        verts.push_back(v3.x); verts.push_back(v3.y); verts.push_back(v3.z);

        actualStarCount++;
    }

    outStarCount = actualStarCount;
    LOGI("Generated %d stars (expected up to 1500)", actualStarCount);
    return verts;
}

std::vector<uint16_t> SkyRenderer::getStarIndices(int starCount) {
    std::vector<uint16_t> idx;
    idx.reserve(starCount * 6);
    for (int i = 0; i < starCount; i++) {
        uint16_t base = (uint16_t)(i * 4);
        idx.push_back(base);
        idx.push_back(base + 1);
        idx.push_back(base + 2);
        idx.push_back(base);
        idx.push_back(base + 2);
        idx.push_back(base + 3);
    }
    return idx;
}



// ===== 日出/日落渐变 =====

std::vector<float> SkyRenderer::getSunriseVertices() {
    // TRIANGLE_FAN：中心 + 17 个环形顶点
    std::vector<float> verts;

    // 中心点
    verts.push_back(0.0f);
    verts.push_back(100.0f);
    verts.push_back(0.0f);

    // 环形顶点（16 段）
    for (int k = 0; k <= 16; k++) {
        float angle = (float)k * (float)(M_PI * 2.0) / 16.0f;
        float sx = sinf(angle);
        float cy = cosf(angle);
        verts.push_back(sx * 120.0f);
        verts.push_back(cy * 120.0f);
        verts.push_back(-cy * 40.0f);
    }

    return verts;
}

std::vector<uint16_t> SkyRenderer::getSunriseIndices() {
    std::vector<uint16_t> idx;

    // TRIANGLE_FAN：从中心点出发
    for (int k = 1; k <= 16; k++) {
        idx.push_back(0);
        idx.push_back((uint16_t)k);
        idx.push_back((uint16_t)(k + 1));
    }

    return idx;
}

// ===== 天体旋转 =====
glm::mat4 SkyRenderer::getCelestialRotation(float normalizedTime) {
    // 直接使用 normalizedTime (0~1)，已包含偏移和曲线
    float angleDeg = normalizedTime * 360.0f;
    glm::mat4 rot(1.0f);
    rot = glm::rotate(rot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    rot = glm::rotate(rot, glm::radians(angleDeg), glm::vec3(1, 0, 0));
    return rot;
}
float SkyRenderer::sunAngle(float timeOfDay) {
    // timeOfDay: 0-24000（0=日出，6000=正午，12000=日落，18000=午夜）
    // 返回太阳角度（弧度）
    return timeOfDay * (float)(M_PI * 2.0) / 24000.0f;
}


// ===== 雨天透明度计算 =====
float SkyRenderer::getCelestialAlpha(bool isMoon) {


    if (isMoon) {//TODO:实现雨天太阳月亮透明度计算
        return 1.0f;
    }
    else {
        return 1.0f;
    }
}

// ===== 计算天空参数 =====
SkyRenderParams SkyRenderer::computeSkyParams(Light* light) {
    SkyRenderParams params;
    params.timeOfDay = 6000.0f;
    params.starBrightness = 0.0f;
    params.normalizedTime = 0.25f;
    params.moonPhase = 0;

    if (light) {
        long long dayTime = light->getWorldDayTime();
        // 月相
        params.moonPhase = (int)((dayTime / 24000LL % 8LL + 8LL) % 8LL);

        params.normalizedTime = light->getNormalizedTime();

        // timeOfDay 设为 normalizedTime * 24000，便于旧函数使用
        params.timeOfDay = params.normalizedTime * 24000.0f;

        // 星星亮度（原版 ClientLevel.getStarBrightness）
        float f = params.normalizedTime;
        float f1 = 1.0F - (std::cos(f * 2.0f * (float)M_PI) * 2.0F + 0.25F);
        f1 = std::clamp(f1, 0.0F, 1.0F);
        params.starBrightness = f1 * f1 * 0.5F;
    }
    return params;
}

const char* SkyRenderer::getMoonPhasePath(int phase) {
    static const char* paths[] = {
            "environment/celestial/moon/full_moon.png",
            "environment/celestial/moon/waning_gibbous.png",
            "environment/celestial/moon/third_quarter.png",
            "environment/celestial/moon/waning_crescent.png",
            "environment/celestial/moon/new_moon.png",
            "environment/celestial/moon/waxing_crescent.png",
            "environment/celestial/moon/first_quarter.png",
            "environment/celestial/moon/waxing_gibbous.png"
    };
    return paths[phase % 8];
}

float SkyRenderer::getStarBrightness(float timeOfDay) {
    float normalizedTime = timeOfDay / 24000.0f;
    float starBrightness = 0.0f;

    // 夜晚星星可见
    if (normalizedTime > 0.5f) {
        float nightProgress = (normalizedTime - 0.5f) * 2.0f;  // 0-1
        starBrightness = 1.0f - fabsf(nightProgress - 0.5f) * 2.0f;
    }

    return starBrightness;
}
