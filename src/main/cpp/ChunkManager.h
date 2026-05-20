#pragma once
#include "Chunk.h"
#include "ChunkParser.h"
#include <unordered_map>
#include <mutex>
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
                   int dimensionMinY = 0);
    
    // 获取区块
    const Chunk* getChunk(int x, int z) const;
    Chunk* getChunk(int x, int z);
    
    // 卸载区块
    void unloadChunk(int x, int z);
    
    // 清空所有区块
    void clear();
    
    // 获取已加载的区块数量
    size_t getLoadedChunkCount() const;
    
    // 获取所有区块（用于渲染）
    std::vector<const Chunk*> getAllChunks() const;

private:
    std::unordered_map<ChunkPos, std::unique_ptr<Chunk>, std::hash<ChunkPos>> chunks;
    mutable std::mutex mutex;
    ChunkParser parser;
};