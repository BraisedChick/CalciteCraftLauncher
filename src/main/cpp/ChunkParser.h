#pragma once
#include "Chunk.h"
#include <vector>
#include <cstdint>
#include <memory>

// 1.17.x（protocol 755~756）区块包携带区块级生物群系数组（每 section 64 个，自底向上拼接），
// 此宏定义时 parseChunkData / ChunkManager::loadChunk 签名条件编译进 chunkBiomes 参数；
// 1.18+ 生物群系内嵌在各 section 的调色板里，无此参数。
// 注意：所有使用该参数的声明/定义/调用点必须以本宏同步（ChunkManager.h 已包含本头文件）。
#if PROTOCOL_VERSION >= 755 && PROTOCOL_VERSION < 757
#define CHUNK_HAS_LEVEL_BIOMES 1
#endif

class ChunkParser {
public:
    ChunkParser();

    // 解析区块数据（通用接口）
    std::unique_ptr<Chunk> parseChunkData(
        int chunkX,
        int chunkZ,
        const std::vector<uint8_t>& data,
        bool fullChunk,
        long long primaryBitMask,
        const std::vector<uint8_t>& heightmaps,
        const std::vector<uint8_t>& blockEntities,
        int dimensionMinY,
        int dimensionHeight
#ifdef CHUNK_HAS_LEVEL_BIOMES
        ,
        const std::vector<int32_t>& chunkBiomes   // 1.17.x 专用：区块级生物群系数组
#endif
    );
    
    // 版本特定的解析方法
    std::unique_ptr<Chunk> parseLegacyChunk(      // 1.8 - 1.12
        int chunkX, int chunkZ,
        const std::vector<uint8_t>& data,
        long long primaryBitMask
    );
    
    std::unique_ptr<Chunk> parseModernChunk(      // 1.13 - 1.16
        int chunkX, int chunkZ,
        const std::vector<uint8_t>& data,
        long long primaryBitMask
    );

    std::unique_ptr<Chunk> parseV17Chunk(         // 1.17.x
        int chunkX, int chunkZ,
        const std::vector<uint8_t>& data,
        long long primaryBitMask,
        const std::vector<uint8_t>& heightmaps,
        const std::vector<int32_t>& chunkBiomes,
        int dimensionMinY,
        int dimensionHeight
    );
    
    std::unique_ptr<Chunk> parseExtendedChunk(    // 1.18+
        int chunkX, int chunkZ,
        const std::vector<uint8_t>& data,
        long long primaryBitMask,
        const std::vector<uint8_t>& heightmaps,
        int dimensionMinY,
        int dimensionHeight
    );
    
    std::unique_ptr<Chunk> parseLatestChunk(      // 1.20.5+
        int chunkX, int chunkZ,
        const std::vector<uint8_t>& data,
        long long primaryBitMask,
        const std::vector<uint8_t>& heightmaps,
        const std::vector<uint8_t>& blockEntities
    );

private:
    // Section 解析（不同版本可能有不同的格式）
    std::unique_ptr<ChunkSection> parseSection(
        const std::vector<uint8_t>& data,
        size_t sectionStart,
        size_t sectionEnd,
        int bitsPerBlock,
        bool hasPalette,
        int paletteLength
    );

    // 解析 biomes 数据（4x4x4 = 64 个 biome ID）
    bool parseBiomes(std::vector<int32_t>& biomesOut,
                     const std::vector<uint8_t>& data,
                     size_t& pos,
                     size_t dataEnd);

    // 辅助函数
    int32_t readVarInt(const std::vector<uint8_t>& data, size_t& pos);
    uint64_t readLong(const std::vector<uint8_t>& data, size_t& pos);
};
