#include "ChunkManager.h"
#include "utils.h"

ChunkManager::ChunkManager() {}

void ChunkManager::loadChunk(int x, int z, const std::vector<uint8_t>& data,
                              bool fullChunk, long long primaryBitMask,
                              const std::vector<uint8_t>& heightmaps,
                              const std::vector<uint8_t>& blockEntities,
                              int dimensionMinY) {
    std::lock_guard<std::mutex> lock(mutex);
    
    try {
        auto chunk = parser.parseChunkData(x, z, data, fullChunk, primaryBitMask, 
                                           heightmaps, blockEntities, dimensionMinY);
        
        ChunkPos pos{x, z};
        chunks[pos] = std::move(chunk);
    } catch (const std::exception& e) {
        LOGE("Failed to load chunk (%d, %d): %s", x, z, e.what());
    }
}

const Chunk* ChunkManager::getChunk(int x, int z) const {
    std::lock_guard<std::mutex> lock(mutex);
    
    ChunkPos pos{x, z};
    auto it = chunks.find(pos);
    if (it != chunks.end()) {
        return it->second.get();
    }
    return nullptr;
}

Chunk* ChunkManager::getChunk(int x, int z) {
    std::lock_guard<std::mutex> lock(mutex);
    
    ChunkPos pos{x, z};
    auto it = chunks.find(pos);
    if (it != chunks.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ChunkManager::unloadChunk(int x, int z) {
    std::lock_guard<std::mutex> lock(mutex);
    
    ChunkPos pos{x, z};
    auto it = chunks.find(pos);
    if (it != chunks.end()) {
        chunks.erase(it);
    }
}

void ChunkManager::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    chunks.clear();
    LOGI("All chunks cleared");
}

size_t ChunkManager::getLoadedChunkCount() const {
    std::lock_guard<std::mutex> lock(mutex);
    return chunks.size();
}

std::vector<const Chunk*> ChunkManager::getAllChunks() const {
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<const Chunk*> result;
    result.reserve(chunks.size());

    for (const auto& pair : chunks) {
        result.push_back(pair.second.get());
    }

    return result;
}
