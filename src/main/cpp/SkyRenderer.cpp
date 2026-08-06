#include "SkyRenderer.h"
#include <cmath>
#include <random>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

// ===== 星星（照抄原版 MC SkyRenderer.buildStars）=====

const std::vector<float>& SkyRenderer::starVertices() {
    // 1500 个固定朝向小方块，分布在半径 100 的球面上
    // 顶点格式：POSITION only（3 floats），每个星星 4 个顶点
    static std::vector<float> verts;
    if (!verts.empty()) return verts;

    std::mt19937 rng(10842);  // 固定种子，与原版一致
    float radius = 100.0f;

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

        // 构建旋转矩阵：rotateTowards(-dir, up) * rotateZ(-roll)
        // 手动实现 rotateTowards（glm::rotation 在某些 GLM 版本缺失）
        glm::vec3 from = glm::normalize(-dir);
        glm::vec3 to   = glm::vec3(0.0f, 1.0f, 0.0f);
        float d = glm::dot(from, to);
        glm::quat q;
        if (d > -0.9999f) {
            glm::vec3 axis = glm::cross(from, to);
            q = glm::quat(1.0f + d, axis.x, axis.y, axis.z);
            q = glm::normalize(q);
        } else {
            // 近似反向：取垂直轴旋转 180°
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

        // 4 个顶点（原版 MC 顺序：(s,-s,0), (s,s,0), (-s,s,0), (-s,-s,0)）
        glm::vec3 v0 = finalRot * glm::vec3( s, -s, 0.0f) + dir;
        glm::vec3 v1 = finalRot * glm::vec3( s,  s, 0.0f) + dir;
        glm::vec3 v2 = finalRot * glm::vec3(-s,  s, 0.0f) + dir;
        glm::vec3 v3 = finalRot * glm::vec3(-s, -s, 0.0f) + dir;

        verts.push_back(v0.x); verts.push_back(v0.y); verts.push_back(v0.z);
        verts.push_back(v1.x); verts.push_back(v1.y); verts.push_back(v1.z);
        verts.push_back(v2.x); verts.push_back(v2.y); verts.push_back(v2.z);
        verts.push_back(v3.x); verts.push_back(v3.y); verts.push_back(v3.z);
    }

    return verts;
}

const std::vector<uint16_t>& SkyRenderer::starIndices() {
    // 每个星星 4 个顶点（POSITION only，3 floats），6 个索引（2 个三角形）
    static std::vector<uint16_t> idx;
    if (!idx.empty()) return idx;

    int starCount = (int)(SkyRenderer::starVertices().size() / 3 / 4);
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
    // 原版 MC：mulPose(YP, -90°) → mulPose(XP, time*360°)
    // time=0: X 旋转 0°→东方地平线；time=0.25: 90°→天顶；time=0.5: 180°→西方地平线
    float normalizedTime = timeOfDay / 24000.0f;
    float angle = (normalizedTime * 360.0f) - 80.0f;  // 减 90° 偏置
    
    glm::mat4 rot(1.0f);
    rot = glm::rotate(rot, glm::radians(-90.0f), glm::vec3(0, 1, 0));
    rot = glm::rotate(rot, glm::radians(angle), glm::vec3(1, 0, 0));
    
    return rot;
}
