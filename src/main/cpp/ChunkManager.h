#pragma once
#include "Chunk.h"
#include "ChunkParser.h"
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <vector>

class ChunkManager {
public:
    ChunkManager();
    
    // 加载区块
    void loadChunk(int x, int z, const std::vector<uint8_t>& data, 
                   bool fullChunk, long long primaryBitMask,
                   const std::vector<uint8_t>& heightmaps,
                   const std::vector<uint8_t>& blockEntities,
                   int dimensionMinY, int dimensionHeight);
    
    // 获取区块（返回 shared_ptr，线程安全）
    std::shared_ptr<const Chunk> getChunk(int x, int z) const;
    std::shared_ptr<Chunk> getChunk(int x, int z);
    
    // 卸载区块
    void unloadChunk(int x, int z);
    
    // 清空所有区块
    void clear();
    
    // 获取已加载的区块数量
    size_t getLoadedChunkCount() const;
    
    // 获取所有区块（用于渲染）
    std::vector<std::shared_ptr<const Chunk>> getAllChunks() const;

private:
    std::unordered_map<ChunkPos, std::shared_ptr<Chunk>, std::hash<ChunkPos>> chunks;
    mutable std::shared_mutex mutex;
    ChunkParser parser;
};