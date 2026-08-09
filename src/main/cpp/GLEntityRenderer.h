#pragma once

#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>

#include "Entity.h"
#include "EntityModel.h"

// 实体渲染器（OpenGL 后端）
// 每个模型只上传一次顶点数据到 GPU，渲染时复用 VAO/VBO
class GLEntityRenderer {
public:
    GLEntityRenderer() = default;
    ~GLEntityRenderer() { cleanup(); }

    // 初始化着色器、VAO 等（须在 OpenGL 线程调用）
    bool init();
    void cleanup();
    bool isInitialized() const { return m_initialized; }

    // 渲染所有实体
    // view, proj : 当前视图和投影矩阵
    // partialTick : 插值因子 (0.0~1.0)
    void renderAll(const std::vector<Entity>& entities,
                   const glm::mat4& view, const glm::mat4& proj,
                   float partialTick);

    // 清除纹理缓存（断开连接时调用）
    void clearTextureCache();

    // 统计信息
    int getRenderedCount() const { return m_renderedCount; }
    int getTotalCount() const { return m_totalCount; }

private:
    // 每个模型的 GPU 资源
    struct GLModelResource {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ibo = 0;          // 索引缓冲（如果有）
        int vertexCount = 0;
        int indexCount = 0;      // 如果有索引
        bool hasIndices = false;
    };

    // 获取或创建模型的 GPU 资源（懒加载）
    const GLModelResource& getModelResource(const EntityModel& model);

    // 渲染不同类型的实体（调用对应的模型绘制）
    void renderHumanoid(float bodyYaw, float headYaw, float headPitch);
    void renderQuadruped(const EntityModel& model, float bodyYaw, float headYaw, float headPitch);
    void renderSpider(float bodyYaw, float headYaw);
    void renderCreeper();
    void renderSlime();
    void renderGhast();
    void renderItem();

    // 纹理加载
    GLuint getEntityTexture(const Entity& entity);

    // OpenGL 资源
    GLuint m_shaderProgram = 0;
    GLint m_uMVP = -1;
    GLint m_uTexture = -1;
    GLint m_uHasTexture = -1;
    GLint m_uColor = -1;
    GLuint m_dummyVao = 0;   // 用于渲染时绑定（实际使用模型的VAO）

    // 模型 → GPU 资源缓存
    std::unordered_map<const EntityModel*, GLModelResource> m_modelCache;

    // 纹理缓存 (实体类型名 → GL纹理ID)
    std::unordered_map<std::string, GLuint> m_textureCache;

    // 统计
    int m_renderedCount = 0;
    int m_totalCount = 0;

    bool m_initialized = false;
};