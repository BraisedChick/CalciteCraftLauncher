#pragma once
#include "Chunk.h"
#include "ChunkManager.h"
#include "VulkanRenderer.h"
#include <vector>
#include <unordered_map>
#include <string>
#include <utility>

class MeshGenerator {
public:
    struct BlockFace {
        float x, y, z;
        float nx, ny, nz;
        float u, v;
        uint8_t r, g, b;
    };

    // 网格生成输出：包含顶点、索引、草覆盖层索引数和水索引数
    struct SectionMeshOutput {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        uint32_t overlayIndexCount = 0;  // 草覆盖层索引数（共面需 LEQUAL，写深度）
        uint32_t waterIndexCount = 0;    // 水索引数（需 alpha blend，不写深度）
    };

    static std::pair<std::vector<Vertex>, std::vector<uint32_t>> generateMeshWithIndices(const Chunk& chunk);
    // 生成 section 网格，接受外部传入的 scratch vectors（避免每次重新分配）
    // baseVertices/Indices, overlayVertices/Indices, waterVertices/Indices
    // 在 worker loop 中复用，clear() 后传入
    static SectionMeshOutput generateSectionMesh(
        const ChunkSection& section,
        int chunkX, int sectionY, int chunkZ,
        const ChunkManager* chunkManager,
        std::vector<Vertex>& baseVertices,
        std::vector<uint32_t>& baseIndices,
        std::vector<Vertex>& overlayVertices,
        std::vector<uint32_t>& overlayIndices,
        std::vector<Vertex>& waterVertices,
        std::vector<uint32_t>& waterIndices);

private:
    static int32_t getBlockStateAt(int x, int y, int z, const ChunkManager* chunkManager);
};