// Botcraft Chunk.cpp - GPLv3 Licensed
// Original source: https://github.com/TheVoxel/ProtocolCraft
// Modified for personal use project

#include "ChunkParser.h"
#include "utils.h"
#include "protocolCraft/BinaryReadWrite.hpp"
#include <cstring>
#include <stdexcept>
#include <android/log.h>

#define LOG_TAG "ChunkParser"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

ChunkParser::ChunkParser() {}

int32_t ChunkParser::readVarInt(const std::vector<uint8_t>& data, size_t& pos) {
    try {
        ProtocolCraft::ReadIterator iter = data.begin() + pos;
        size_t length = data.size() - pos;
        int32_t result = ProtocolCraft::ReadData<int32_t, ProtocolCraft::VarInt>(iter, length);
        pos = data.size() - length;
        return result;
    } catch (const std::exception& e) {
        LOGE("Failed to read VarInt: %s", e.what());
        throw;
    }
}

uint64_t ChunkParser::readLong(const std::vector<uint8_t>& data, size_t& pos) {
    if (pos + 8 > data.size()) {
        throw std::runtime_error("Not enough data to read long");
    }
    
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value = (value << 8) | data[pos++];
    }
    return value;
}

std::unique_ptr<ChunkSection> ChunkParser::parseSection(
    const std::vector<uint8_t>& data,
    size_t sectionStart,
    size_t sectionEnd,
    int bitsPerBlock,
    bool hasPalette,
    int paletteLength
) {
    auto section = std::make_unique<ChunkSection>();
    size_t pos = sectionStart;

    if (bitsPerBlock == 0) {
        // Empty section (Single Value palette)
        section->isEmpty = true;
        return section;
    }

    // Parse palette (only for Section Palette, not Global Palette)
    std::vector<int32_t> palette;
    if (hasPalette && paletteLength > 0) {
        palette.resize(paletteLength);
        for (int i = 0; i < paletteLength && pos < sectionEnd; i++) {
            palette[i] = readVarInt(data, pos);
        }
    }

    // Read data array length
    if (pos + 4 > sectionEnd) {
        section->isEmpty = true;
        return section;
    }
    
    int dataArrayLength = readVarInt(data, pos);
    
    // Read long array
    std::vector<uint64_t> dataLongs(dataArrayLength);
    for (int i = 0; i < dataArrayLength && pos + 8 <= sectionEnd; i++) {
        dataLongs[i] = readLong(data, pos);
    }
    
    // Decompress block states
    section->blockStates.resize(4096, 0);
    
    int bitOffset = 0;
    for (int i = 0; i < 4096; i++) {
        int longIndex = bitOffset / 64;
        int bitInLong = bitOffset % 64;
        
        if (longIndex >= static_cast<int>(dataLongs.size())) {
            break;
        }
        
        uint64_t mask = ((1ULL << bitsPerBlock) - 1);
        uint64_t blockState;
        
        // Check if the value spans across two longs
        if (bitInLong + bitsPerBlock <= 64) {
            // Value fits in one long
            blockState = (dataLongs[longIndex] >> bitInLong) & mask;
        } else {
            // Value spans two longs
            int remainingBits = 64 - bitInLong;
            uint64_t firstPart = (dataLongs[longIndex] >> bitInLong);
            uint64_t secondPart = 0;
            
            if (longIndex + 1 < static_cast<int>(dataLongs.size())) {
                secondPart = dataLongs[longIndex + 1];
            }
            
            blockState = firstPart | (secondPart << remainingBits);
            blockState &= mask;
        }
        
        // Apply palette or use global ID
        // For Section Palette (hasPalette=true): use palette[index]
        // For Global Palette (hasPalette=false): use blockState directly
        if (hasPalette && !palette.empty() && blockState < static_cast<size_t>(palette.size())) {
            section->blockStates[i] = palette[blockState];
        } else {
            // Global Palette or fallback: use raw block state ID
            section->blockStates[i] = static_cast<int32_t>(blockState);
        }
        
        bitOffset += bitsPerBlock;
        
        // From protocol version 713 (1.16+), entries don't span across multiple longs
        #if PROTOCOL_VERSION > 712
        if (64 - (bitOffset % 64) < bitsPerBlock) {
            bitOffset += 64 - (bitOffset % 64);
        }
        #endif
    }
    
    section->isEmpty = false;
    return section;
}

std::unique_ptr<Chunk> ChunkParser::parseChunkData(
    int chunkX,
    int chunkZ,
    const std::vector<uint8_t>& data,
    bool fullChunk,
    long long primaryBitMask,
    const std::vector<uint8_t>& heightmapsData,
    const std::vector<uint8_t>& blockEntitiesData,
    int dimensionMinY
) {
    // 编译期根据 PROTOCOL_VERSION 宏选择区块解析方法
#if PROTOCOL_VERSION >= 766
    // 1.20.5+ Latest
    return parseLatestChunk(chunkX, chunkZ, data, primaryBitMask,
                            heightmapsData, blockEntitiesData);
#elif PROTOCOL_VERSION >= 757
    // 1.18+ Extended
    return parseExtendedChunk(chunkX, chunkZ, data, primaryBitMask,
                              heightmapsData, dimensionMinY);
#elif PROTOCOL_VERSION >= 404
    // 1.13-1.17 Modern
    return parseModernChunk(chunkX, chunkZ, data, primaryBitMask);
#else
    // 1.8-1.12 Legacy
    return parseLegacyChunk(chunkX, chunkZ, data, primaryBitMask);
#endif
}

// ===== Legacy Chunk Parser (1.8 - 1.12) =====
std::unique_ptr<Chunk> ChunkParser::parseLegacyChunk(
    int chunkX, int chunkZ,
    const std::vector<uint8_t>& data,
    long long primaryBitMask
) {
    LOGI("Parsing legacy chunk (1.8-1.12)");
    
    auto chunk = std::make_unique<Chunk>(chunkX, chunkZ);
    // TODO: 实现 1.8-1.12 的区块解析逻辑
    // - 全局调色板
    // - Section 高度 16
    // - Y 范围 0-255
    
    chunk->isLoaded = true;
    return chunk;
}

// ===== Modern Chunk Parser (1.13 - 1.17) =====
std::unique_ptr<Chunk> ChunkParser::parseModernChunk(
    int chunkX, int chunkZ,
    const std::vector<uint8_t>& data,
    long long primaryBitMask
) {
    LOGI("Parsing modern chunk (1.13-1.17)");
    
    auto chunk = std::make_unique<Chunk>(chunkX, chunkZ);
    // TODO: 实现 1.13-1.17 的区块解析逻辑
    // - Section 调色板
    // - Section 高度 16
    // - Y 范围 0-255
    
    chunk->isLoaded = true;
    return chunk;
}

// ===== Extended Chunk Parser (1.18+) =====
bool ChunkParser::parseBiomes(std::vector<int32_t>& biomesOut,
                               const std::vector<uint8_t>& data,
                               size_t& pos,
                               size_t dataEnd) {
    if (pos >= dataEnd) return false;

    biomesOut.resize(64, 0);
    uint8_t bitsPerBiome = data[pos++];

    if (bitsPerBiome == 0) {
        // 单值调色板：所有位置相同
        int32_t singleBiome = readVarInt(data, pos);
        for (int i = 0; i < 64; i++) {
            biomesOut[i] = singleBiome;
        }
        // 消耗 data array（PalettedContainer 总是序列化 data array，即使 bits=0）
        int dataArrayLength = readVarInt(data, pos);
        pos += dataArrayLength * 8;
        return true;
    }

    // 解析调色板（bitsPerBiome <= 3 时有调色板，> 3 时是全局 ID）
    std::vector<int32_t> palette;
    bool hasPalette = (bitsPerBiome <= 3);
    if (hasPalette) {
        int paletteLength = readVarInt(data, pos);
        palette.resize(paletteLength);
        for (int i = 0; i < paletteLength && pos < dataEnd; i++) {
            palette[i] = readVarInt(data, pos);
        }
    }

    // 读数据数组长度
    if (pos >= dataEnd) return false;
    int dataArrayLength = readVarInt(data, pos);

    // 读 long 数组
    std::vector<uint64_t> dataLongs(dataArrayLength);
    for (int i = 0; i < dataArrayLength && pos + 8 <= dataEnd; i++) {
        dataLongs[i] = readLong(data, pos);
    }

    // 解压 64 个 biome ID，使用和 blockStates 相同的位操作逻辑
    int bitOffset = 0;
    for (int i = 0; i < 64; i++) {
        int longIndex = bitOffset / 64;
        int bitInLong = bitOffset % 64;

        if (longIndex >= static_cast<int>(dataLongs.size())) break;

        uint64_t mask = ((1ULL << bitsPerBiome) - 1);
        uint64_t biomeId;

        if (bitInLong + bitsPerBiome <= 64) {
            biomeId = (dataLongs[longIndex] >> bitInLong) & mask;
        } else {
            int remainingBits = 64 - bitInLong;
            uint64_t firstPart = (dataLongs[longIndex] >> bitInLong);
            uint64_t secondPart = (longIndex + 1 < static_cast<int>(dataLongs.size())) ? dataLongs[longIndex + 1] : 0;
            biomeId = firstPart | (secondPart << remainingBits);
            biomeId &= mask;
        }

        if (hasPalette && !palette.empty() && biomeId < static_cast<uint64_t>(palette.size())) {
            biomesOut[i] = palette[biomeId];
        } else {
            biomesOut[i] = static_cast<int32_t>(biomeId);
        }

        bitOffset += bitsPerBiome;

        // 1.16+ 协议中，compact data 的单个条目不会跨 long 边界
        // 如果当前 long 剩余位数不够存一个条目，则跳过 padding 到下一个 long
        #if PROTOCOL_VERSION > 712
        if (64 - (bitOffset % 64) < bitsPerBiome) {
            bitOffset += 64 - (bitOffset % 64);
        }
        #endif
    }

    return true;
}

std::unique_ptr<Chunk> ChunkParser::parseExtendedChunk(
    int chunkX, int chunkZ,
    const std::vector<uint8_t>& data,
    long long primaryBitMask,
    const std::vector<uint8_t>& heightmapsData,
    int dimensionMinY
) {

	auto chunk = std::make_unique<Chunk>(chunkX, chunkZ);
    size_t pos = 0;
    
    // 用实际维度参数覆盖 Chunk 默认维度（主世界 vs 下界/末地）
    if (dimensionMinY != chunk->dimension.minY) {
        chunk->dimension.minY = dimensionMinY;
        chunk->dimension.maxY = dimensionMinY + ((int)chunk->sections.size() * SECTION_HEIGHT);
    }
    
    // Initialize all sections as empty
    for (size_t i = 0; i < chunk->sections.size(); i++) {
        if (!chunk->sections[i]) {
            auto emptySection = std::make_unique<ChunkSection>();
            emptySection->y = chunk->dimension.minY + (i * SECTION_HEIGHT);
            emptySection->isEmpty = true;
            chunk->sections[i] = std::move(emptySection);
        }
    }
    
    // Parse ALL sections sequentially (Botcraft style - no bitmask)
    // Minecraft 1.18+ sends all sections in order, including empty ones
    int sectionCount = chunk->dimension.getSectionCount();
    for (int sectionY = 0; sectionY < sectionCount; sectionY++) {
        // Check if we have enough data for block_count
        if (pos + 2 > data.size()) {
            LOGE("Not enough data for section %d at pos=%zu, data.size=%zu", sectionY, pos, data.size());
            break;
        }
        
        // Read block count (2 bytes, BIG-ENDIAN)
        uint16_t blockCount = (static_cast<uint16_t>(data[pos]) << 8) | data[pos+1];
        pos += 2;
        
        // If block_count is 0, this section is empty - skip all data
        if (blockCount == 0) {
            // Still need to read bits_per_block and skip the rest
            if (pos >= data.size()) break;
            uint8_t bitsPerBlock = data[pos++];
            
            // Skip palette if present
            if (bitsPerBlock > 0 && bitsPerBlock <= 8) {
                int paletteLength = readVarInt(data, pos);
                for (int i = 0; i < paletteLength; i++) {
                    readVarInt(data, pos);
                }
            } else if (bitsPerBlock == 0) {
                // Single value palette - read the value
                readVarInt(data, pos);
            }
            
            // Skip data array
            int dataArrayLength = readVarInt(data, pos);
            pos += dataArrayLength * 8;
            
            // ========== Parse Biomes for this section (1.18+) ==========
            if (pos < data.size() && chunk->sections[sectionY]) {
                parseBiomes(chunk->sections[sectionY]->biomes, data, pos, data.size());
            }

            continue;
        }

        // Non-empty section: parse it
        if (pos >= data.size()) break;
        uint8_t bitsPerBlock = data[pos++];
        
        bool hasPalette = (bitsPerBlock <= 8);
        int paletteLength = 0;
        
        if (hasPalette) {
            paletteLength = readVarInt(data, pos);
        }
        
        size_t sectionStart = pos;
        
        // Skip palette
        if (hasPalette) {
            for (int i = 0; i < paletteLength; i++) {
                readVarInt(data, pos);
            }
        } else if (bitsPerBlock == 0) {
            // Single value palette
            readVarInt(data, pos);
        }
        
        // Read data array length
        int dataArrayLength = readVarInt(data, pos);
        
        size_t sectionEnd = pos + (dataArrayLength * 8);
        if (sectionEnd > data.size()) {
            LOGE("Section data exceeds buffer size");
            break;
        }
        
        // Parse the section
        auto section = parseSection(data, sectionStart, sectionEnd,
                                   bitsPerBlock, hasPalette, paletteLength);
        
        if (section) {
            section->y = chunk->dimension.minY + (sectionY * SECTION_HEIGHT);
            chunk->sections[sectionY] = std::move(section);
        }
        
        // Move position past this section's data
        pos = sectionEnd;
        
        // ========== Parse Biomes for this section (1.18+) ==========
        if (pos < data.size() && chunk->sections[sectionY]) {
            parseBiomes(chunk->sections[sectionY]->biomes, data, pos, data.size());
        }
    }
    
    chunk->heightmaps = heightmapsData;
    chunk->isLoaded = true;
    return chunk;
}

// ===== Latest Chunk Parser (1.20.5+) =====
std::unique_ptr<Chunk> ChunkParser::parseLatestChunk(
    int chunkX, int chunkZ,
    const std::vector<uint8_t>& data,
    long long primaryBitMask,
    const std::vector<uint8_t>& heightmapsData,
    const std::vector<uint8_t>& blockEntitiesData
) {
    LOGI("Parsing latest chunk (1.20.5+)");
    
    auto chunk = std::make_unique<Chunk>(chunkX, chunkZ);
    // TODO: 实现 1.20.5+ 的区块解析逻辑
    // - Registry-based 系统
    // - 可能的新格式
    
    chunk->heightmaps = heightmapsData;
    chunk->blockEntities = blockEntitiesData;
    chunk->isLoaded = true;
    return chunk;
}
