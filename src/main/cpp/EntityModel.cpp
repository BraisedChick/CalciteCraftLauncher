#include "EntityModel.h"
#include <glm/glm.hpp>    // 仅用于 vec3 运算
#include <cstring>

// ---------- 静态成员定义 ----------
EntityModel EntityModel::s_humanoid;
EntityModel EntityModel::s_quadruped;
EntityModel EntityModel::s_cow;
EntityModel EntityModel::s_spider;
EntityModel EntityModel::s_creeper;
EntityModel EntityModel::s_slime;
EntityModel EntityModel::s_ghast;
EntityModel EntityModel::s_item;
bool EntityModel::s_initialized = false;

// ---------- 辅助：设置默认布局 ----------
void EntityModel::setupDefaultLayout() {
    // 所有顶点格式：位置 (x,y,z) + 纹理坐标 (u,v)，全部 float
    m_layout.stride = 5 * sizeof(float);
    m_layout.attributes = {
            {0, 0, 3, VertexComponentType::FLOAT, false},          // location 0: pos
            {1, 3 * sizeof(float), 2, VertexComponentType::FLOAT, false} // location 1: uv
    };
}

// ---------- 几何构建工具 ----------
void EntityModel::addFace(const glm::vec3& p0, const glm::vec3& p1,
                          const glm::vec3& p2, const glm::vec3& p3,
                          float u, float v, float faceW, float faceH,
                          float texW, float texH) {
    float u0 = u / texW, v0 = v / texH;
    float u1 = (u + faceW) / texW, v1 = (v + faceH) / texH;

    auto addVert = [&](const glm::vec3& p, float tu, float tv) {
        // 像素单位 → block 单位 (1px = 1/16 block)
        m_vertices.push_back(p.x / 16.0f);
        m_vertices.push_back(p.y / 16.0f);
        m_vertices.push_back(p.z / 16.0f);
        m_vertices.push_back(tu);
        m_vertices.push_back(tv);
    };

    // 两个三角形构成矩形
    addVert(p0, u0, v0);
    addVert(p1, u1, v0);
    addVert(p2, u1, v1);
    addVert(p0, u0, v0);
    addVert(p2, u1, v1);
    addVert(p3, u0, v1);
}

void EntityModel::buildBox(float ox, float oy, float oz,
                           float w, float h, float d,
                           float u, float v, float texW, float texH) {
    glm::vec3 p0(ox, oy + h, oz);
    glm::vec3 p1(ox + w, oy + h, oz);
    glm::vec3 p2(ox + w, oy, oz);
    glm::vec3 p3(ox, oy, oz);
    glm::vec3 p4(ox, oy + h, oz + d);
    glm::vec3 p5(ox + w, oy + h, oz + d);
    glm::vec3 p6(ox + w, oy, oz + d);
    glm::vec3 p7(ox, oy, oz + d);

    // 六个面：上下前后左右（UV 布局参照 MC 标准）
    addFace(p4, p5, p1, p0, u + d, v, w, d, texW, texH);      // Top
    addFace(p3, p2, p6, p7, u + d + w, v, w, d, texW, texH);  // Bottom
    addFace(p0, p1, p2, p3, u + d, v + d, w, h, texW, texH);  // Front
    addFace(p5, p4, p7, p6, u + d + w + d, v + d, w, h, texW, texH); // Back
    addFace(p4, p0, p3, p7, u, v + d, d, h, texW, texH);      // Left
    addFace(p1, p5, p6, p2, u + d + w, v + d, d, h, texW, texH); // Right
}

// ---------- 各个模型的构建函数 ----------
EntityModel EntityModel::buildHumanoid() {
    EntityModel model;
    const float TW = 64.0f, TH = 64.0f;

    model.buildBox(-4, 24, -4, 8, 8, 8, 0, 0, TW, TH);          // Head
    model.buildBox(-4, 12, -2, 8, 12, 4, 16, 16, TW, TH);       // Body
    model.buildBox(-8, 12, -2, 4, 12, 4, 40, 16, TW, TH);       // Right Arm
    model.buildBox(4, 12, -2, 4, 12, 4, 32, 48, TW, TH);        // Left Arm
    model.buildBox(-4, 0, -2, 4, 12, 4, 0, 16, TW, TH);         // Right Leg
    model.buildBox(0, 0, -2, 4, 12, 4, 16, 48, TW, TH);         // Left Leg

    model.setupDefaultLayout();
    return model;
}

EntityModel EntityModel::buildQuadruped() {
    EntityModel model;
    const float QW = 64.0f, QH = 32.0f;

    // Head
    model.buildBox(-4, 8, -14, 8, 8, 8, 0, 0, QW, QH);

    // Body (旋转后的长方体，UV 映射特殊处理)
    {
        float bx = -5.0f, by = 6.0f, bz = -8.0f;
        float bw = 10.0f, bh = 8.0f, bd = 16.0f;
        float uw = 10.0f, uh = 16.0f, ud = 8.0f;
        float bu = 28.0f, bv = 8.0f;
        glm::vec3 bp0(bx, by + bh, bz);
        glm::vec3 bp1(bx + bw, by + bh, bz);
        glm::vec3 bp2(bx + bw, by, bz);
        glm::vec3 bp3(bx, by, bz);
        glm::vec3 bp4(bx, by + bh, bz + bd);
        glm::vec3 bp5(bx + bw, by + bh, bz + bd);
        glm::vec3 bp6(bx + bw, by, bz + bd);
        glm::vec3 bp7(bx, by, bz + bd);
        model.addFace(bp0, bp1, bp2, bp3, bu + ud, bv, uw, ud, QW, QH);
        model.addFace(bp5, bp4, bp7, bp6, bu + ud + uw, bv, uw, ud, QW, QH);
        model.addFace(bp4, bp5, bp1, bp0, bu + ud + uw + ud, bv + ud, uw, uh, QW, QH);
        model.addFace(bp3, bp2, bp6, bp7, bu + ud, bv + ud, uw, uh, QW, QH);
        model.addFace(bp4, bp0, bp3, bp7, bu, bv + ud, ud, uh, QW, QH);
        model.addFace(bp1, bp5, bp6, bp2, bu + ud + uw, bv + ud, ud, uh, QW, QH);
    }

    // Legs (4条)
    model.buildBox(-5, 0, -7, 4, 6, 4, 0, 16, QW, QH);
    model.buildBox(1, 0, -7, 4, 6, 4, 0, 16, QW, QH);
    model.buildBox(-5, 0, 5, 4, 6, 4, 0, 16, QW, QH);
    model.buildBox(1, 0, 5, 4, 6, 4, 0, 16, QW, QH);

    model.setupDefaultLayout();
    return model;
}

EntityModel EntityModel::buildCow() {
    EntityModel model;
    const float QW = 64.0f, QH = 32.0f;

    model.buildBox(-4, 16, -14, 8, 8, 6, 0, 0, QW, QH);  // Head (smaller)

    // Body (旋转)
    {
        float bx = -6.0f, by = 12.0f, bz = -8.0f;
        float bw = 12.0f, bh = 10.0f, bd = 18.0f;
        float uw = 12.0f, uh = 18.0f, ud = 10.0f;
        float bu = 18.0f, bv = 4.0f;
        glm::vec3 bp0(bx, by + bh, bz);
        glm::vec3 bp1(bx + bw, by + bh, bz);
        glm::vec3 bp2(bx + bw, by, bz);
        glm::vec3 bp3(bx, by, bz);
        glm::vec3 bp4(bx, by + bh, bz + bd);
        glm::vec3 bp5(bx + bw, by + bh, bz + bd);
        glm::vec3 bp6(bx + bw, by, bz + bd);
        glm::vec3 bp7(bx, by, bz + bd);
        model.addFace(bp0, bp1, bp2, bp3, bu + ud, bv, uw, ud, QW, QH);
        model.addFace(bp5, bp4, bp7, bp6, bu + ud + uw, bv, uw, ud, QW, QH);
        model.addFace(bp4, bp5, bp1, bp0, bu + ud + uw + ud, bv + ud, uw, uh, QW, QH);
        model.addFace(bp3, bp2, bp6, bp7, bu + ud, bv + ud, uw, uh, QW, QH);
        model.addFace(bp4, bp0, bp3, bp7, bu, bv + ud, ud, uh, QW, QH);
        model.addFace(bp1, bp5, bp6, bp2, bu + ud + uw, bv + ud, ud, uh, QW, QH);
    }

    // Legs (higher)
    model.buildBox(-6, 0, -8, 4, 12, 4, 0, 16, QW, QH);
    model.buildBox(2, 0, -8, 4, 12, 4, 0, 16, QW, QH);
    model.buildBox(-6, 0, 5, 4, 12, 4, 0, 16, QW, QH);
    model.buildBox(2, 0, 5, 4, 12, 4, 0, 16, QW, QH);

    model.setupDefaultLayout();
    return model;
}

EntityModel EntityModel::buildSpider() {
    EntityModel model;
    const float QW = 64.0f, QH = 32.0f;

    model.buildBox(-7, 4, -4, 14, 8, 8, 0, 0, QW, QH);   // Body
    model.buildBox(-4, 4, -12, 8, 8, 8, 32, 0, QW, QH);   // Head

    model.setupDefaultLayout();
    return model;
}

EntityModel EntityModel::buildCreeper() {
    EntityModel model;
    const float QW = 64.0f, QH = 32.0f;

    model.buildBox(-4, 18, -4, 8, 8, 8, 0, 0, QW, QH);
    model.buildBox(-2, 6, -2, 4, 16, 4, 16, 16, QW, QH);
    model.buildBox(-6, 0, -2, 4, 6, 4, 0, 16, QW, QH);
    model.buildBox(2, 0, -2, 4, 6, 4, 0, 16, QW, QH);
    model.buildBox(-6, 0, 2, 4, 6, 4, 0, 16, QW, QH);
    model.buildBox(2, 0, 2, 4, 6, 4, 0, 16, QW, QH);

    model.setupDefaultLayout();
    return model;
}

EntityModel EntityModel::buildSlime() {
    EntityModel model;
    const float QW = 64.0f, QH = 32.0f;

    model.buildBox(-8, 0, -8, 16, 16, 16, 0, 0, QW, QH);  // Outer
    model.buildBox(-4, 4, -4, 8, 8, 8, 0, 16, QW, QH);    // Core

    model.setupDefaultLayout();
    return model;
}

EntityModel EntityModel::buildGhast() {
    EntityModel model;
    const float QW = 64.0f, QH = 32.0f;

    model.buildBox(-8, 8, -8, 16, 16, 16, 0, 0, QW, QH);

    float tentX[] = {-5.5f, -5.5f, -5.5f, 0, 0, 0, 5.5f, 5.5f, 5.5f};
    float tentZ[] = {-5.5f, 0, 5.5f, -5.5f, 0, 5.5f, -5.5f, 0, 5.5f};
    int tentLen[] = {11, 12, 9, 14, 10, 13, 8, 11, 15};
    for (int i = 0; i < 9; i++) {
        model.buildBox(tentX[i] - 1, 8 - tentLen[i], tentZ[i] - 1,
                       2, tentLen[i], 2, 0, 0, QW, QH);
    }

    model.setupDefaultLayout();
    return model;
}

EntityModel EntityModel::buildItem() {
    EntityModel model;
    model.buildBox(-2, -2, -2, 4, 4, 4, 0, 0, 16.0f, 16.0f);
    model.setupDefaultLayout();
    return model;
}

// ---------- 对外接口 ----------
void EntityModel::initializeAll() {
    if (s_initialized) return;
    s_humanoid = buildHumanoid();
    s_quadruped = buildQuadruped();
    s_cow = buildCow();
    s_spider = buildSpider();
    s_creeper = buildCreeper();
    s_slime = buildSlime();
    s_ghast = buildGhast();
    s_item = buildItem();
    s_initialized = true;
}

const EntityModel& EntityModel::getHumanoid() { return s_humanoid; }
const EntityModel& EntityModel::getQuadruped() { return s_quadruped; }
const EntityModel& EntityModel::getCow() { return s_cow; }
const EntityModel& EntityModel::getSpider() { return s_spider; }
const EntityModel& EntityModel::getCreeper() { return s_creeper; }
const EntityModel& EntityModel::getSlime() { return s_slime; }
const EntityModel& EntityModel::getGhast() { return s_ghast; }
const EntityModel& EntityModel::getItem() { return s_item; }