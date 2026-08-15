#include "EntityModel.h"
#include <glm/glm.hpp>    // 仅用于 vec3 运算
#include <cstring>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
// ---------- 静态成员定义 ----------
EntityModel EntityModel::s_humanoid;
EntityModel EntityModel::s_quadruped;
EntityModel EntityModel::s_cow;
EntityModel EntityModel::s_spider;
EntityModel EntityModel::s_creeper;
EntityModel EntityModel::s_slime;
EntityModel EntityModel::s_ghast;
EntityModel EntityModel::s_skeleton;
EntityModel EntityModel::s_sheep;
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

void EntityModel::buildRotatedBox(float ox, float oy, float oz,
                                  float w, float h, float d,
                                  float u, float v,
                                  float texW, float texH,
                                  const glm::vec3& axis, float angle,
                                  const glm::vec3& translation) {
    // 六个面的局部顶点
    // 顶面 (y = oy+h)
    glm::vec3 top[4] = { {ox, oy + h, oz}, {ox + w, oy + h, oz}, {ox + w, oy + h, oz + d}, {ox, oy + h, oz + d} };
    // 底面 (y = oy)
    glm::vec3 bottom[4] = { {ox, oy, oz}, {ox + w, oy, oz}, {ox + w, oy, oz + d}, {ox, oy, oz + d} };
    // 前面 (z = oz)
    glm::vec3 front[4] = { {ox, oy + h, oz}, {ox + w, oy + h, oz}, {ox + w, oy, oz}, {ox, oy, oz} };
    // 后面 (z = oz + d)
    glm::vec3 back[4] = { {ox, oy + h, oz + d}, {ox + w, oy + h, oz + d}, {ox + w, oy, oz + d}, {ox, oy, oz + d} };
    // 左面 (x = ox)
    glm::vec3 left[4] = { {ox, oy + h, oz}, {ox, oy + h, oz + d}, {ox, oy, oz + d}, {ox, oy, oz} };
    // 右面 (x = ox + w)
    glm::vec3 right[4] = { {ox + w, oy + h, oz}, {ox + w, oy + h, oz + d}, {ox + w, oy, oz + d}, {ox + w, oy, oz} };

    // 应用旋转和平移
    glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angle, axis);
    glm::mat4 trans = glm::translate(glm::mat4(1.0f), translation);
    glm::mat4 transform = trans * rot;

    auto transformPoint = [&](const glm::vec3& p) {
        glm::vec4 v = transform * glm::vec4(p, 1.0f);
        return glm::vec3(v);
    };

    glm::vec3 tTop[4], tBottom[4], tFront[4], tBack[4], tLeft[4], tRight[4];
    for (int i = 0; i < 4; ++i) {
        tTop[i] = transformPoint(top[i]);
        tBottom[i] = transformPoint(bottom[i]);
        tFront[i] = transformPoint(front[i]);
        tBack[i] = transformPoint(back[i]);
        tLeft[i] = transformPoint(left[i]);
        tRight[i] = transformPoint(right[i]);
    }

    // 纹理尺寸对应几何尺寸
    float uw = w, uh = h, ud = d;

    // 六个面，纹理参数与手动构建一致
    addFace(tTop[0], tTop[1], tTop[2], tTop[3], u + ud,      v,         uw, ud, texW, texH);
    addFace(tBottom[0], tBottom[1], tBottom[2], tBottom[3], u + ud + uw, v,         uw, ud, texW, texH);
    addFace(tFront[0], tFront[1], tFront[2], tFront[3], u + ud + uw + ud, v + ud, uw, uh, texW, texH);
    addFace(tBack[0], tBack[1], tBack[2], tBack[3], u + ud,      v + ud, uw, uh, texW, texH);
    addFace(tLeft[0], tLeft[1], tLeft[2], tLeft[3], u,           v + ud, ud, uh, texW, texH);
    addFace(tRight[0], tRight[1], tRight[2], tRight[3], u + ud + uw, v + ud, ud, uh, texW, texH);
}
void EntityModel::addPart(const std::string& name, int startVertex, int vertexCount, const glm::vec3& pivot) {
    // 存储时转为块单位（1px = 1/16 block）
    m_parts.push_back({name, startVertex, vertexCount, pivot / 16.0f});
}

// ---------- 各个模型的构建函数 ----------
EntityModel EntityModel::buildHumanoid() {
    EntityModel model;
    const float TW = 64.0f, TH = 64.0f;

    // 使用 helper lambda 简化
    auto addBoxPart = [&](const std::string& name,
                          float ox, float oy, float oz,
                          float w, float h, float d,
                          float u, float v,
                          const glm::vec3& pivot) {
        int startVertex = (int)model.m_vertices.size() / 5;  // 当前顶点数（总 float / 5）
        model.buildBox(ox, oy, oz, w, h, d, u, v, TW, TH);
        int vertexCount = (int)model.m_vertices.size() / 5 - startVertex;
        model.addPart(name, startVertex, vertexCount, pivot);
    };

    // Head
    addBoxPart("head", -4, 24, -4, 8, 8, 8, 0, 0, glm::vec3(0, 24, 0));

    // Body
    addBoxPart("body", -4, 12, -2, 8, 12, 4, 16, 16, glm::vec3(0, 12, 0));

    // Right Arm
    addBoxPart("right_arm", -8, 12, -2, 4, 12, 4, 40, 16, glm::vec3(-5, 18, 0));

    // Left Arm
    addBoxPart("left_arm", 4, 12, -2, 4, 12, 4, 40, 16, glm::vec3(5, 18, 0));

    // Right Leg
    addBoxPart("right_leg", -4, 0, -2, 4, 12, 4, 0, 16, glm::vec3(-2, 12, 0));

    // Left Leg
    addBoxPart("left_leg", 0, 0, -2, 4, 12, 4, 0, 16, glm::vec3(2, 12, 0));

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

EntityModel EntityModel::buildSkeleton() {
    EntityModel model;
    const float TW = 64.0f, TH = 32.0f;

    auto addBoxPart = [&](const std::string& name,
                          float ox, float oy, float oz,
                          float w, float h, float d,
                          float u, float v,
                          const glm::vec3& pivot) {
        int startVertex = (int)model.m_vertices.size() / 5;
        model.buildBox(ox, oy, oz, w, h, d, u, v, TW, TH);
        int vertexCount = (int)model.m_vertices.size() / 5 - startVertex;
        model.addPart(name, startVertex, vertexCount, pivot);
    };

    addBoxPart("head", -4, 24, -4, 8, 8, 8, 0, 0, glm::vec3(0, 24, 0));
    addBoxPart("body", -4, 12, -2, 8, 12, 4, 16, 16, glm::vec3(0, 12, 0));
    addBoxPart("right_arm", -6, 12, -1, 2, 12, 2, 40, 16, glm::vec3(-5, 18, 0));
    addBoxPart("left_arm", 4, 12, -1, 2, 12, 2, 40, 16, glm::vec3(5, 18, 0));
    addBoxPart("right_leg", -3, 0, -1, 2, 12, 2, 0, 16, glm::vec3(-2, 12, 0));
    addBoxPart("left_leg", 1, 0, -1, 2, 12, 2, 0, 16, glm::vec3(2, 12, 0));

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

EntityModel EntityModel::buildSheep() {
    EntityModel model;
    const float TW = 64.0f, TH = 32.0f;

    // 头部
    model.buildBox(-3, 14, -14, 6, 6, 8, 0, 0, TW, TH);

    // 身体
    model.buildRotatedBox(-4, -10, -7, 8, 16, 6, 28, 8, TW, TH,
                          glm::vec3(1,0,0), glm::radians(90.0f),
                          glm::vec3(0, 11, 2));

    // 四条腿
    model.buildBox(-5, 0,  5, 4, 12, 4, 0, 16, TW, TH);
    model.buildBox( 1, 0,  5, 4, 12, 4, 0, 16, TW, TH);
    model.buildBox(-5, 0, -7, 4, 12, 4, 0, 16, TW, TH);
    model.buildBox( 1, 0, -7, 4, 12, 4, 0, 16, TW, TH);

    model.setupDefaultLayout();
    return model;
}
EntityModel EntityModel::buildCreeper() {
    EntityModel model;
    const float QW = 64.0f, QH = 32.0f;

    // ---- 腿（底部，Y=0~6） ----
    // 右后腿：Z=2（后腿）
    model.buildBox(-4, 0, 2, 4, 6, 4, 0, 16, QW, QH);
    // 左后腿：Z=2
    model.buildBox(0, 0, 2, 4, 6, 4, 0, 16, QW, QH);
    // 右前腿：Z=-6（前腿）
    model.buildBox(-4, 0, -6, 4, 6, 4, 0, 16, QW, QH);
    // 左前腿：Z=-6
    model.buildBox(0, 0, -6, 4, 6, 4, 0, 16, QW, QH);

    // ---- 身体（中间，Y=6~18） ----
    model.buildBox(-4, 6, -2, 8, 12, 4, 16, 16, QW, QH);

    // ---- 头部（顶部，Y=18~26） ----
    model.buildBox(-4, 18, -4, 8, 8, 8, 0, 0, QW, QH);

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
    s_skeleton = buildSkeleton();
    s_sheep = buildSheep();
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
const EntityModel& EntityModel::getSkeleton() { return s_skeleton; }
const EntityModel& EntityModel::getItem() { return s_item; }
const EntityModel& EntityModel::getSheep() { return s_sheep; }