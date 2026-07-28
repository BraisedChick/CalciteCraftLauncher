#pragma once
#include <vector>
#include <cstdint>
#include <array>
#include <memory>

// Minecraft 区块尺寸（动态配置）
constexpr int CHUNK_WIDTH = 16;
constexpr int CHUNK_DEPTH = 16;
constexpr int SECTION_HEIGHT = 16; // Section 高度通常为 16

// 维度配置（编译期默认值，运行时由 ChunkParser 覆盖）
struct DimensionConfig {
    int minY = 0;
    int maxY = 256;

    int getSectionCount() const { return (maxY - minY) / SECTION_HEIGHT; }
};

// 编译期默认维度配置：1.18+ 为 -64~320，早期为 0~256
constexpr DimensionConfig getDefaultDimensionConfig() {
#if PROTOCOL_VERSION >= 757
    return {-64, 320};
#else
    return {0, 256};
#endif
}

// 区块坐标
struct ChunkPos {
    int x;
    int z;

    bool operator==(const ChunkPos& other) const {
        return x == other.x && z == other.z;
    }
};

// 哈希函数用于 std::unordered_map
namespace std {
    template<>
    struct hash<ChunkPos> {
        size_t operator()(const ChunkPos& pos) const {
            return hash<int>()(pos.x) ^ (hash<int>()(pos.z) << 1);
        }
    };
}

// 区块截面（16x16x16）
struct ChunkSection {
    int y; // 截面的 Y 坐标
    std::vector<int32_t> blockStates; // 方块状态数据（完整的 blockState ID，非截断值）
    std::vector<int32_t> biomes; // 生物群系数据 (4x4x4 = 64 个 biome ID)
    bool isEmpty = true;

    // 光照数据（2048 bytes = 4096 blocks × 4 bits，每 byte 存两个值）
    std::vector<uint8_t> skyLight;
    std::vector<uint8_t> blockLight;

    ChunkSection() : y(0) {}
    ChunkSection(int yVal) : y(yVal), isEmpty(true) {}

    // 获取某方块的光照值（坐标 0-15）
    uint8_t getSkyLight(int x, int y, int z) const {
        if (skyLight.size() != 2048) return 15;
        int idx = ((y & 15) * 16 + (z & 15)) * 16 + (x & 15);
        int byteIdx = idx / 2;
        return (skyLight[byteIdx] >> ((idx & 1) * 4)) & 0xF;
    }

    uint8_t getBlockLight(int x, int y, int z) const {
        if (blockLight.size() != 2048) return 0;
        int idx = ((y & 15) * 16 + (z & 15)) * 16 + (x & 15);
        int byteIdx = idx / 2;
        return (blockLight[byteIdx] >> ((idx & 1) * 4)) & 0xF;
    }

    // 设置某方块的光照值
    void setSkyLight(int x, int y, int z, uint8_t val) {
        // 懒分配填 15（0xFF=两个 nibble 各 15）而非 0：与 getSkyLight 缺数据
        // 默认 15 保持一致，避免光照引擎首写把其余 4095 格从"隐含 15"塌成 0
        if (skyLight.size() != 2048) skyLight.assign(2048, 0xFF);
        int idx = ((y & 15) * 16 + (z & 15)) * 16 + (x & 15);
        int byteIdx = idx / 2;
        int shift = (idx & 1) * 4;
        skyLight[byteIdx] = (skyLight[byteIdx] & ~(0xF << shift)) | ((val & 0xF) << shift);
    }

    void setBlockLight(int x, int y, int z, uint8_t val) {
        if (blockLight.size() != 2048) blockLight.resize(2048, 0);
        int idx = ((y & 15) * 16 + (z & 15)) * 16 + (x & 15);
        int byteIdx = idx / 2;
        int shift = (idx & 1) * 4;
        blockLight[byteIdx] = (blockLight[byteIdx] & ~(0xF << shift)) | ((val & 0xF) << shift);
    }
};

// 完整区块
struct Chunk {
    ChunkPos pos;
    std::vector<std::unique_ptr<ChunkSection>> sections; // 动态大小的 sections
    std::vector<uint8_t> heightmaps;
    std::vector<uint8_t> blockEntities;
    bool isLoaded = false;
    
    // 维度配置（编译期默认值，运行时由 ChunkParser 覆盖）
    DimensionConfig dimension = getDefaultDimensionConfig();

    Chunk() : pos({0, 0}) {
        initializeSections();
    }
    
    Chunk(int x, int z) : pos({x, z}) {
        initializeSections();
    }
    
    // 初始化 sections（根据维度配置）
    void initializeSections() {
        int sectionCount = dimension.getSectionCount();
        sections.resize(sectionCount);
        
        // 初始化每个 section
        for (int i = 0; i < sectionCount; i++) {
            int sectionY = dimension.minY + (i * SECTION_HEIGHT);
            sections[i] = std::make_unique<ChunkSection>(sectionY);
        }
    }

    // 获取方块的区块内坐标 (blockX: 0-15, blockY: minY~maxY, blockZ: 0-15)
    // 返回方块状态 ID
    uint32_t getBlockState(int blockX, int blockY, int blockZ) const {
        if (blockX < 0 || blockX >= 16 || blockZ < 0 || blockZ >= 16) {
            return 0;
        }
        if (blockY < dimension.minY || blockY >= dimension.maxY) {
            return 0;
        }
        int sectionIndex = (blockY - dimension.minY) / SECTION_HEIGHT;
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(sections.size())) {
            return 0;
        }
        const auto& section = sections[sectionIndex];
        if (!section) return 0;
        int localY = blockY - section->y;
        if (localY < 0 || localY >= SECTION_HEIGHT) return 0;
        int index = (localY * CHUNK_DEPTH + blockZ) * CHUNK_WIDTH + blockX;
        if (index >= 0 && index < static_cast<int>(section->blockStates.size())) {
            return section->blockStates[index];
        }
        return 0;
    }

    // 设置一个方块的状态（blockX/Z: 0-15, blockY: 绝对坐标）
    void setBlockState(int blockX, int blockY, int blockZ, int32_t state) {
        if (blockX < 0 || blockX >= 16 || blockZ < 0 || blockZ >= 16) return;
        if (blockY < dimension.minY || blockY >= dimension.maxY) return;
        int sectionIndex = (blockY - dimension.minY) / SECTION_HEIGHT;
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(sections.size())) return;
        auto& section = sections[sectionIndex];
        if (!section) return;
        int localY = blockY - section->y;
        if (localY < 0 || localY >= SECTION_HEIGHT) return;
        int index = (localY * CHUNK_DEPTH + blockZ) * CHUNK_WIDTH + blockX;
        if (index >= 0 && index < static_cast<int>(section->blockStates.size())) {
            section->blockStates[index] = state;
            section->isEmpty = false;
        }
    }
};
