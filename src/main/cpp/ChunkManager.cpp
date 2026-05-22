#include "ChunkManager.h"
#include "utils.h"

ChunkManager::ChunkManager() {}

void ChunkManager::loadChunk(int x, int z, const std::vector<uint8_t>& data,
                              bool fullChunk, long long primaryBitMask,
                              const std::vector<uint8_t>& heightmaps,
                              const std::vector<uint8_t>& blockEntities,
                              int dimensionMinY) {
    std::unique_lock<std::shared_mutex> lock(mutex);

    try {
        auto chunk = parser.parseChunkData(x, z, data, fullChunk, primaryBitMask,
                                           heightmaps, blockEntities, dimensionMinY);

        ChunkPos pos{x, z};
        chunks[pos] = std::move(chunk);
    } catch (const std::exception& e) {
        LOGE("Failed to load chunk (%d, %d): %s", x, z, e.what());
    }
}

std::shared_ptr<const Chunk> ChunkManager::getChunk(int x, int z) const {
    std::shared_lock<std::shared_mutex> lock(mutex);

    ChunkPos pos{x, z};
    auto it = chunks.find(pos);
    if (it != chunks.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<Chunk> ChunkManager::getChunk(int x, int z) {
    std::shared_lock<std::shared_mutex> lock(mutex);

    ChunkPos pos{x, z};
    auto it = chunks.find(pos);
    if (it != chunks.end()) {
        return it->second;
    }
    return nullptr;
}

void ChunkManager::unloadChunk(int x, int z) {
    std::unique_lock<std::shared_mutex> lock(mutex);

    ChunkPos pos{x, z};
    auto it = chunks.find(pos);
    if (it != chunks.end()) {
        chunks.erase(it);
    }
}

void ChunkManager::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex);
    chunks.clear();
    LOGI("All chunks cleared");
}

size_t ChunkManager::getLoadedChunkCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex);
    return chunks.size();
}

std::vector<std::shared_ptr<const Chunk>> ChunkManager::getAllChunks() const {
    std::shared_lock<std::shared_mutex> lock(mutex);

    std::vector<std::shared_ptr<const Chunk>> result;
    result.reserve(chunks.size());

    for (const auto& pair : chunks) {
        result.push_back(pair.second);
    }

    return result;
}
