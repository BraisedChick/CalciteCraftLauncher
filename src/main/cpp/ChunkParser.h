#pragma once
#include "Chunk.h"
#include <vector>
#include <cstdint>
#include <memory>

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
        int dimensionMinY = 0
    );
    
    // 版本特定的解析方法
    std::unique_ptr<Chunk> parseLegacyChunk(      // 1.8 - 1.12
        int chunkX, int chunkZ,
        const std::vector<uint8_t>& data,
        long long primaryBitMask
    );
    
    std::unique_ptr<Chunk> parseModernChunk(      // 1.13 - 1.17
        int chunkX, int chunkZ,
        const std::vector<uint8_t>& data,
        long long primaryBitMask
    );
    
    std::unique_ptr<Chunk> parseExtendedChunk(    // 1.18+
        int chunkX, int chunkZ,
        const std::vector<uint8_t>& data,
        long long primaryBitMask,
        const std::vector<uint8_t>& heightmaps,
        int dimensionMinY
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
