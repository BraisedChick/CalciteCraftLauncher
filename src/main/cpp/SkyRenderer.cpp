#include "SkyRenderer.h"
#include <cmath>
#include <random>
#include <glm/gtc/matrix_transform.hpp>

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

const std::vector<float>& SkyRenderer::topSkyVertices() {
    static std::vector<float> verts = buildSkyDisc(16.0f);
    return verts;
}

const std::vector<float>& SkyRenderer::bottomSkyVertices() {
    static std::vector<float> verts = buildSkyDisc(-16.0f);
    return verts;
}

// ===== 太阳 =====

const std::vector<float>& SkyRenderer::sunVertices() {
    // 60×60 quad at y=100
    static std::vector<float> verts = {
        // x,    y,     z,    u,   v
        -30.0f, 100.0f, -30.0f, 0.0f, 0.0f,
         30.0f, 100.0f, -30.0f, 1.0f, 0.0f,
         30.0f, 100.0f,  30.0f, 1.0f, 1.0f,
        -30.0f, 100.0f,  30.0f, 0.0f, 1.0f,
    };
    return verts;
}

const std::vector<uint16_t>& SkyRenderer::sunIndices() {
    static std::vector<uint16_t> idx = { 0, 1, 2, 0, 2, 3 };
    return idx;
}

// ===== 月亮 =====

const std::vector<float>& SkyRenderer::moonVertices() {
    // 40×40 quad at y=-100
    static std::vector<float> verts = {
        // x,     y,      z,     u,   v
        -20.0f, -100.0f,  20.0f, 0.0f, 0.0f,
         20.0f, -100.0f,  20.0f, 1.0f, 0.0f,
         20.0f, -100.0f, -20.0f, 1.0f, 1.0f,
        -20.0f, -100.0f, -20.0f, 0.0f, 1.0f,
    };
    return verts;
}

const std::vector<uint16_t>& SkyRenderer::moonIndices() {
    static std::vector<uint16_t> idx = { 0, 1, 2, 0, 2, 3 };
    return idx;
}

// ===== 星星 =====

const std::vector<float>& SkyRenderer::starVertices() {
    // 1500 个随机小方块，分布在半径 100 的球面上
    static std::vector<float> verts;
    if (!verts.empty()) return verts;

    std::mt19937 rng(10842);  // 固定种子，与原版一致
    int count = 1500;
    float radius = 100.0f;
    float starSize = 0.15f;

    for (int i = 0; i < count; i++) {
        // 随机方向（球面均匀分布）
        float f1 = (float)rng() / (float)rng.max() * 2.0f - 1.0f;
        float f2 = (float)rng() / (float)rng.max() * 2.0f - 1.0f;
        float f3 = (float)rng() / (float)rng.max() * 2.0f - 1.0f;
        float lenSq = f1*f1 + f2*f2 + f3*f3;
        if (lenSq < 0.01f || lenSq > 1.0f) continue;

        // 归一化到球面
        float len = sqrtf(lenSq);
        float dx = f1 / len * radius;
        float dy = f2 / len * radius;
        float dz = f3 / len * radius;

        // 生成 billboard 顶点（中心位置 + 偏移，着色器中展开）
        float s = starSize + (float)rng() / (float)rng.max() * 0.1f;
        float offsets[][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int j = 0; j < 4; j++) {
            verts.push_back(dx); verts.push_back(dy); verts.push_back(dz);  // 中心
            verts.push_back(offsets[j][0] * s); verts.push_back(offsets[j][1] * s);  // 偏移
        }
    }

    return verts;
}

const std::vector<uint16_t>& SkyRenderer::starIndices() {
    // 每个星星 6 个索引（2 个三角形）
    static std::vector<uint16_t> idx;
    if (!idx.empty()) return idx;

    int starCount = (int)(SkyRenderer::starVertices().size() / 5 / 4);  // 每个顶点 5 个 float，每个星星 4 个顶点
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

const std::vector<float>& SkyRenderer::sunriseVertices() {
    // TRIANGLE_FAN：中心 + 17 个环形顶点
    static std::vector<float> verts;
    if (!verts.empty()) return verts;

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

const std::vector<uint16_t>& SkyRenderer::sunriseIndices() {
    static std::vector<uint16_t> idx;
    if (!idx.empty()) return idx;

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

glm::mat4 SkyRenderer::celestialRotation(float timeOfDay) {
    // 原版 MC：先绕 Y 轴 -90°，再绕 X 轴旋转 sunAngle * 360°
    float angle = sunAngle(timeOfDay) * 360.0f / (float)(M_PI * 2.0);
    
    glm::mat4 rot(1.0f);
    rot = glm::rotate(rot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    rot = glm::rotate(rot, glm::radians(angle), glm::vec3(1, 0, 0));
    
    return rot;
}
