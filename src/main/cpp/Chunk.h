#pragma once
#include <vector>
#include <cstdint>
#include <array>
#include <memory>
#include "MinecraftVersion.h"

// Minecraft 区块尺寸（动态配置）
constexpr int CHUNK_WIDTH = 16;
constexpr int CHUNK_DEPTH = 16;
constexpr int SECTION_HEIGHT = 16; // Section 高度通常为 16

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
    std::vector<uint8_t> blockStates; // 方块状态数据
    std::vector<uint8_t> biomes; // 生物群系数据
    bool isEmpty = true;

    ChunkSection() : y(0) {}
    ChunkSection(int yVal) : y(yVal), isEmpty(true) {}
};

// 完整区块
struct Chunk {
    ChunkPos pos;
    std::vector<std::unique_ptr<ChunkSection>> sections; // 动态大小的 sections
    std::vector<uint8_t> heightmaps;
    std::vector<uint8_t> blockEntities;
    bool isLoaded = false;
    
    // 维度配置（从 VersionManager 获取）
    DimensionConfig dimension;

    Chunk() : pos({0, 0}) {
        initializeSections();
    }
    
    Chunk(int x, int z) : pos({x, z}) {
        initializeSections();
    }
    
    // 初始化 sections（根据维度配置）
    void initializeSections() {
        const auto& versionMgr = VersionManager::getInstance();
        dimension = versionMgr.getDimensionConfig();
        
        int sectionCount = dimension.getSectionCount();
        sections.resize(sectionCount);
        
        // 初始化每个 section
        for (int i = 0; i < sectionCount; i++) {
            int sectionY = dimension.minY + (i * SECTION_HEIGHT);
            sections[i] = std::make_unique<ChunkSection>(sectionY);
        }
    }

    // 获取方块的绝对坐标 (blockX: 0-15, blockY: minY~maxY, blockZ: 0-15)
    // 返回方块状态 ID
    uint32_t getBlockState(int blockX, int blockY, int blockZ) const {
        if (blockX < 0 || blockX >= 16 || blockZ < 0 || blockZ >= 16) {
            return 0; // 空气
        }

        if (blockY < dimension.minY || blockY >= dimension.maxY) {
            return 0; // 空气
        }

        // 计算截面索引
        int sectionIndex = (blockY - dimension.minY) / SECTION_HEIGHT;
        if (sectionIndex < 0 || sectionIndex >= static_cast<int>(sections.size())) {
            return 0;
        }

        const auto& section = sections[sectionIndex];
        if (!section) {
            return 0; // 截面不存在
        }
        
        // TODO: 实际解析 blockStates 数据
        // 暂时简化：如果截面不为空，就认为是草方块 (ID=1)
        return 1; // 草方块
    }
};
