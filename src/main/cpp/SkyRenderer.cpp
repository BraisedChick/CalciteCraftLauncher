#include "SkyRenderer.h"
#include <cmath>
#include <random>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <android/log.h>

#define LOG_TAG "SkyRenderer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

float SkyRenderer::sunAngle(float timeOfDay) {
    // timeOfDay: 0-24000（0=日出，6000=正午，12000=日落，18000=午夜）
    // 返回太阳角度（弧度）
    return timeOfDay * (float)(M_PI * 2.0) / 24000.0f;
}

glm::mat4 SkyRenderer::getCelestialRotation(float timeOfDay) {
    // 原版 MC：mulPose(YP, -90°) → mulPose(XP, time*360°)
    // time=0: X 旋转 0°→东方地平线；time=0.25: 90°→天顶；time=0.5: 180°→西方地平线
    float normalizedTime = timeOfDay / 24000.0f;
    float angle = (normalizedTime * 360.0f) - 90.0f;  // 减 90° 偏置

    glm::mat4 rot(1.0f);
    rot = glm::rotate(rot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    rot = glm::rotate(rot, glm::radians(angle), glm::vec3(1, 0, 0));

    return rot;
}


// ===== 动画计算 =====

float SkyRenderer::getCelestialAlpha(float timeOfDay, bool isMoon) {
    float normalizedTime = timeOfDay / 24000.0f;

    if (isMoon) {
        // 月亮在夜晚可见（0.5-1.0）
        if (normalizedTime < 0.5f) {
            // 从日落到午夜 (0.5-0.75)
            if (normalizedTime < 0.25f) return 0.0f;
            float t = (normalizedTime - 0.25f) * 4.0f;  // 0.25-0.5 -> 0-1
            return t * t * (3.0f - 2.0f * t);  // 平滑步函数
        } else if (normalizedTime < 0.75f) {
            return 1.0f;  // 全夜
        } else {
            // 从午夜到日出 (0.75-1.0)
            float t = (normalizedTime - 0.75f) * 4.0f;  // 0.75-1.0 -> 0-1
            return 1.0f - t * t * (3.0f - 2.0f * t);  // 平滑步函数
        }
    } else {
        // 太阳在白天可见 (0.0-0.5)
        if (normalizedTime > 0.5f) {
            // 从日落到午夜
            if (normalizedTime < 0.75f) {
                float t = (normalizedTime - 0.5f) * 4.0f;  // 0.5-0.75 -> 0-1
                return 1.0f - t * t * (3.0f - 2.0f * t);
            } else {
                return 0.0f;
            }
        } else if (normalizedTime < 0.25f) {
            // 从日出到正午
            if (normalizedTime < 0.125f) {
                float t = normalizedTime * 8.0f;  // 0-0.125 -> 0-1
                return t * t * (3.0f - 2.0f * t);
            } else {
                return 1.0f;
            }
        } else if (normalizedTime < 0.375f) {
            return 1.0f;  // 正午时段
        } else {
            // 从正午到日落
            float t = (normalizedTime - 0.375f) * 8.0f;  // 0.375-0.5 -> 0-1
            return 1.0f - t * t * (3.0f - 2.0f * t);
        }
    }
}

glm::vec3 SkyRenderer::getSkyColor(float timeOfDay) {
    float normalizedTime = timeOfDay / 24000.0f;

    // 基础天空颜色（白天浅蓝，夜晚深蓝）
    glm::vec3 baseColor(0.5f, 0.7f, 1.0f);  // 浅蓝色

    // 夜晚变暗
    if (normalizedTime > 0.5f) {
        float nightFactor = 1.0f;
        if (normalizedTime < 0.75f) {
            nightFactor = 0.3f + 0.4f * (normalizedTime - 0.5f) * 4.0f;
        } else {
            nightFactor = 0.7f - 0.4f * (normalizedTime - 0.75f) * 4.0f;
        }
        baseColor *= nightFactor;
    }

    // 日出日落时的橙色/红色
    float sunriseSunset = 0.0f;
    if (normalizedTime < 0.25f) {
        // 日出
        sunriseSunset = sin(normalizedTime * 4.0f * M_PI);
    } else if (normalizedTime > 0.75f) {
        // 日落
        sunriseSunset = sin((normalizedTime - 0.75f) * 4.0f * M_PI);
    }

    if (sunriseSunset > 0.0f) {
        // 混合橙色
        glm::vec3 sunsetColor(1.0f, 0.6f, 0.3f);
        baseColor = baseColor * (1.0f - sunriseSunset * 0.5f) + sunsetColor * sunriseSunset * 0.5f;
    }

    return baseColor;
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
