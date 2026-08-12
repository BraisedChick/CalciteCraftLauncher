#pragma once

#include <vector>
#include <cstdint>
#include "glm/vec3.hpp"

// 顶点组件类型（API 无关）
enum class VertexComponentType : uint8_t {
    FLOAT,
    UINT8,
    UINT16,
    // 可按需扩展
};
// 单个顶点属性描述
struct VertexAttribute {
    uint32_t location;      // shader location (OpenGL attrib index / Vulkan location)
    uint32_t offset;        // 字节偏移量
    uint32_t size;          // 分量个数 (1, 2, 3, 4)
    VertexComponentType type;
    bool normalized;        // 是否归一化（仅对整数类型有效）
};

// 顶点布局描述
struct VertexLayout {
    std::vector<VertexAttribute> attributes;
    uint32_t stride;        // 每个顶点总字节数
};

// ===== 纯数据模型，不依赖任何图形 API =====
class EntityModel {
public:
    struct ModelPart {
        std::string name;
        int startVertex;       // 在 m_vertices 中的起始顶点索引
        int vertexCount;       // 顶点数量
        glm::vec3 pivot;       // 旋转轴心（相对于实体脚底）
    };

    // 获取部件列表（只读）
    const std::vector<ModelPart>& getParts() const { return m_parts; }

    // 获取原始顶点数据（交错的 x,y,z,u,v，全部 float）
    const std::vector<float>& getVertices() const { return m_vertices; }

    // 获取索引数据（当前为空，保留接口）
    const std::vector<uint32_t>& getIndices() const { return m_indices; }

    // 获取顶点布局描述
    const VertexLayout& getLayout() const { return m_layout; }

    // 获取顶点数量（用于绘制调用）
    size_t getVertexCount() const { return m_vertices.size() / (m_layout.stride / sizeof(float)); }

    // ===== 静态工厂方法 =====
    static const EntityModel& getHumanoid();
    static const EntityModel& getQuadruped();
    static const EntityModel& getCow();
    static const EntityModel& getSpider();
    static const EntityModel& getCreeper();
    static const EntityModel& getSlime();
    static const EntityModel& getGhast();
    static const EntityModel& getItem();
    static const EntityModel& getSkeleton();

    // 初始化所有模型（在渲染前调用一次）
    static void initializeAll();

private:
    // 私有构造函数，只能通过静态构建函数创建
    EntityModel() = default;

    // 各模型的构建函数（返回 EntityModel 实例）
    static EntityModel buildHumanoid();
    static EntityModel buildQuadruped();
    static EntityModel buildCow();
    static EntityModel buildSpider();
    static EntityModel buildCreeper();
    static EntityModel buildSlime();
    static EntityModel buildGhast();
    static EntityModel buildItem();
    static EntityModel buildSkeleton();
    // 添加部件（内部使用）
    void addPart(const std::string& name, int startVertex, int vertexCount, const glm::vec3& pivot);

    std::vector<ModelPart> m_parts;   // 部件数据

    // ===== 几何构建辅助（成员函数，直接填充 m_vertices） =====
    void addFace(const glm::vec3& p0, const glm::vec3& p1,
                 const glm::vec3& p2, const glm::vec3& p3,
                 float u, float v, float faceW, float faceH,
                 float texW, float texH);

    void buildBox(float ox, float oy, float oz,
                  float w, float h, float d,
                  float u, float v, float texW, float texH);

    // 设置默认布局（x,y,z,u,v 全 float）
    void setupDefaultLayout();

    // ===== 数据成员 =====
    std::vector<float> m_vertices;
    std::vector<uint32_t> m_indices;   // 暂未使用，保留
    VertexLayout m_layout;

    // ===== 静态缓存 =====
    static EntityModel s_humanoid;
    static EntityModel s_quadruped;
    static EntityModel s_cow;
    static EntityModel s_spider;
    static EntityModel s_creeper;
    static EntityModel s_slime;
    static EntityModel s_ghast;
    static EntityModel s_item;
    static EntityModel s_skeleton;
    static bool s_initialized;
};