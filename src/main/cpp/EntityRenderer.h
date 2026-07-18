#pragma once

#include "Entity.h"
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <vector>
#include <string>

// 实体渲染器（MC 风格盒子模型 + 实体纹理）
// 第一版：简化为 6 面盒子拼装，支持人形/四足两种体型
class EntityRenderer {
public:
    static EntityRenderer& getInstance();

    // 初始化着色器和 VAO/VBO（GL 线程调用一次）
    bool init();
    void cleanup();
    bool isInitialized() const { return initialized; }

    // 渲染所有实体（每帧调用）
    // view/proj: 当前视图和投影矩阵
    // partialTick: 帧插值因子 (0.0~1.0)
    void renderAll(const std::vector<Entity>& entities,
                   const glm::mat4& view, const glm::mat4& proj,
                   float partialTick);

    // 断开连接时清理纹理缓存
    void clearTextureCache();

    // 实体渲染统计（F3 显示用）
    int getRenderedCount() const { return renderedCount; }
    int getTotalCount() const { return totalCount; }

private:
    EntityRenderer() = default;

    // 渲染单个实体
    void renderEntity(const Entity& entity, float partialTick);

    // 渲染人形实体（僵尸、骷髅、玩家等）
    void renderHumanoid(float bodyYaw, float headYaw, float headPitch);

    // 渲染四足动物（猪、牛、羊等）
    void renderQuadruped(const std::vector<float>& verts, float bodyYaw, float headYaw, float headPitch);

    // 渲染蜘蛛（八足扁体）
    void renderSpider(float bodyYaw, float headYaw);

    // 渲染苦力怕
    void renderCreeper();
    // 渲染史莱姆
    void renderSlime();
    // 渲染恶魂
    void renderGhast();
    // 渲染掉落物
    void renderItem();

    // 获取实体对应的 GL 纹理 ID（懒加载）
    GLuint getEntityTexture(const Entity& entity);

    // 构建盒子几何（pos offset, size, UV base on 64x64 texture）
    void buildBox(std::vector<float>& verts,
                  float ox, float oy, float oz,
                  float w, float h, float d,
                  float u, float v, float texW, float texH);

    // 创建一个面（4个顶点）
    void addFace(std::vector<float>& verts,
                 const glm::vec3& p0, const glm::vec3& p1,
                 const glm::vec3& p2, const glm::vec3& p3,
                 float u, float v, float faceW, float faceH,
                 float texW, float texH);

    bool initialized = false;
    GLuint shaderProgram = 0;
    GLint uMVP = -1;
    GLint uTexture = -1;
    GLint uHasTexture = -1;
    GLint uColor = -1;
    GLuint vao = 0, vbo = 0;

    // 盒子顶点数据缓存（预构建）
    std::vector<float> humanoidVerts;
    std::vector<float> quadrupedVerts;
    std::vector<float> cowVerts;
    std::vector<float> spiderVerts;
    std::vector<float> creeperVerts;
    std::vector<float> slimeVerts;
    std::vector<float> ghastVerts;
    std::vector<float> itemVerts;

    // 实体纹理缓存（type name → GL texture ID）
    std::unordered_map<std::string, GLuint> textureCache;

    // 渲染统计
    int renderedCount = 0;
    int totalCount = 0;
};
