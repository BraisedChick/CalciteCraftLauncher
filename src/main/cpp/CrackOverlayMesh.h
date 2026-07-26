#pragma once

#include <vector>
#include <cstdint>
#include "CommonTypes.h"

// 方块破坏覆盖层网格（裂纹动画）
// 纯逻辑层：只负责脏检查与顶点/索引生成，不接触任何图形 API。
// GLRenderer / VulkanRenderer 拿到 Vertex 数据后各自压缩、上传、绘制。
class CrackOverlayMesh {
public:
    // 每帧调用。返回 false 表示当前无需绘制（未在挖掘 / 阶段无效 / 纹理缺失）。
    // rebuilt 输出 true 表示几何体因方块位置或破坏阶段变化而重建，
    // 渲染器需重新上传顶点数据（GL 侧还应在 VAO 失效时无条件重传）。
    bool update(bool& rebuilt);

    const std::vector<Vertex>& getVertices() const { return vertices; }
    const std::vector<uint32_t>& getIndices() const { return indices; }

private:
    // 生成 6 个面的立方体（24 顶点，36 索引）
    void buildMesh(int bx, int by, int bz, float texLayer);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // 脏检查缓存
    int lastBlockX = -9999999;
    int lastBlockY = -9999999;
    int lastBlockZ = -9999999;
    int lastStage = -1;
};
