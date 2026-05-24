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

    static std::vector<Vertex> generateMesh(const Chunk& chunk);
    static std::pair<std::vector<Vertex>, std::vector<uint32_t>> generateMeshWithIndices(const Chunk& chunk);
    static SectionMeshOutput generateSectionMesh(
        const ChunkSection& section,
        int chunkX, int sectionY, int chunkZ,
        const ChunkManager* chunkManager);

private:
    static void addFace(std::vector<Vertex>& vertices, float x, float y, float z,
                       float nx, float ny, float nz,
                       float r, float g, float b);
    static uint32_t getBlockColor(uint16_t blockId);
    // 辅助函数：获取全局坐标的 blockState
    static int32_t getBlockStateAt(int x, int y, int z, const ChunkManager* chunkManager);
};