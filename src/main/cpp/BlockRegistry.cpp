#include "BlockRegistry.h"
#include "TextureAtlas.h"
#include <fstream>
#include <sstream>
#include <android/log.h>

#define LOG_TAG "BlockRegistry"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool BlockRegistry::loadFromJson(const std::string& jsonPath) {
    LOGI("Starting to load blocks from: %s", jsonPath.c_str());
    
    // 检查文件是否存在
    std::ifstream testFile(jsonPath);
    if (!testFile.good()) {
        LOGE("blocks.json file does not exist at: %s", jsonPath.c_str());
        return false;
    }
    testFile.close();
    LOGI("blocks.json file exists, opening for parsing...");

    // 读取 JSON 文件
    std::ifstream file(jsonPath);
    if (!file.is_open()) {
        LOGE("Failed to open blocks.json: %s", jsonPath.c_str());
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    file.close();

    LOGI("Loaded blocks.json (%zu bytes)", json.size());
    
    if (json.empty()) {
        LOGE("blocks.json is empty!");
        return false;
    }

    // 简单的 JSON 解析（针对 blocks.json 格式）
    // 查找所有方块对象
    size_t pos = 0;
    int blockCount = 0;

    while ((pos = json.find("{", pos)) != std::string::npos) {
        size_t endPos = json.find("}", pos);
        if (endPos == std::string::npos) break;

        std::string blockJson = json.substr(pos, endPos - pos + 1);

        // 提取字段
        BlockInfo info;
        info.id = extractInt(blockJson, "\"id\"");
        info.name = extractString(blockJson, "\"name\"");
        info.displayName = extractString(blockJson, "\"displayName\"");
        info.minStateId = extractInt(blockJson, "\"minStateId\"");
        info.maxStateId = extractInt(blockJson, "\"maxStateId\"");
        info.defaultState = extractInt(blockJson, "\"defaultState\"");

        // 验证数据有效性
        if (info.name.empty() || info.minStateId < 0) {
            pos = endPos + 1;
            continue;
        }

        // 存储方块信息（先保存索引，避免 vector 重分配导致指针失效）
        size_t blockIndex = blocks.size();
        blocks.push_back(info);
        
        // 构建 blockState ID → BlockInfo 映射（使用索引而非指针）
        for (int32_t stateId = info.minStateId; stateId <= info.maxStateId; ++stateId) {
            stateToBlock[stateId] = blockIndex;
        }

        blockCount++;
        pos = endPos + 1;

        // 每解析 100 个方块输出一次日志
        if (blockCount % 100 == 0) {
            LOGI("Parsed %d blocks...", blockCount);
        }
    }

    loaded = true;
    LOGI("Successfully loaded %d blocks with %zu state mappings", 
         blockCount, stateToBlock.size());
    
    return true;
}

std::string BlockRegistry::getBlockName(int32_t blockState) const {
    auto it = stateToBlock.find(blockState);
    if (it != stateToBlock.end() && it->second < blocks.size()) {
        return blocks[it->second].name;
    }
    return "unknown_" + std::to_string(blockState);
}

const BlockInfo* BlockRegistry::getBlockInfo(int32_t blockState) const {
    auto it = stateToBlock.find(blockState);
    if (it != stateToBlock.end() && it->second < blocks.size()) {
        return &blocks[it->second];
    }
    return nullptr;
}

// 辅助函数：提取字符串值
std::string BlockRegistry::extractString(const std::string& json, const std::string& key) const {
    std::string searchKey = key + ": \"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";

    pos += searchKey.length();
    size_t endPos = json.find("\"", pos);
    if (endPos == std::string::npos) return "";

    return json.substr(pos, endPos - pos);
}

// 辅助函数：提取整数值
int32_t BlockRegistry::extractInt(const std::string& json, const std::string& key) const {
    std::string searchKey = key + ": ";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return -1;

    pos += searchKey.length();
    
    // 跳过空格
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) {
        pos++;
    }

    // 提取数字
    std::string numStr;
    while (pos < json.size() && (isdigit(json[pos]) || json[pos] == '-')) {
        numStr += json[pos];
        pos++;
    }

    if (numStr.empty()) return -1;

    try {
        return std::stoi(numStr);
    } catch (...) {
        return -1;
    }
}

const BlockMetadata& BlockRegistry::getBlockMetadata(int32_t blockState) const {
    // 读锁：多个 worker 线程可同时读缓存
    {
        std::shared_lock<std::shared_mutex> lock(metadataMutex);
        auto it = metadataCache.find(blockState);
        if (it != metadataCache.end()) return it->second;
    }

    BlockMetadata meta = computeMetadata(blockState);

    // 写锁：仅第一次插入时需要独占
    std::lock_guard<std::shared_mutex> lock(metadataMutex);
    auto result = metadataCache.emplace(blockState, std::move(meta));
    return result.first->second;
}

BlockMetadata BlockRegistry::computeMetadata(int32_t blockState) const {
    BlockMetadata meta;
    const auto* info = getBlockInfo(blockState);
    if (!info) {
        meta.isFullBlock = false;
        meta.height = 0.0f;
        return meta;
    }
    meta.name = info->name;

    // ---- 方块类型判断 ----
    meta.isGrassBlock = (meta.name == "grass_block");
    meta.isLeaves = (meta.name.find("leaves") != std::string::npos);
    meta.isSnow = (meta.name == "snow");
    meta.isPlant = (meta.name == "grass" || meta.name == "tall_grass"
        || meta.name == "fern" || meta.name == "large_fern"
        || meta.name == "dead_bush" || meta.name == "vine"
        || meta.name == "lily_pad" || meta.name == "sugar_cane"
        || meta.name == "brown_mushroom" || meta.name == "red_mushroom"
        || meta.name == "dandelion" || meta.name == "poppy" || meta.name == "blue_orchid"
        || meta.name == "allium" || meta.name == "azure_bluet" || meta.name == "oxeye_daisy"
        || meta.name == "cornflower" || meta.name == "lily_of_the_valley"
        || meta.name == "wither_rose" || meta.name == "sunflower"
        || meta.name == "lilac" || meta.name == "rose_bush" || meta.name == "peony");

    // ---- 水 ----
    meta.isWater = (meta.name == "water");

    // ---- 高度 ----
    if (meta.isSnow) {
        int stateCount = info->maxStateId - info->minStateId + 1;
        if (stateCount >= 8) {
            int layers = (blockState - info->minStateId) + 1;
            if (layers < 1) layers = 1;
            if (layers > 8) layers = 8;
            meta.height = layers / 8.0f;
        } else {
            meta.height = 0.5f;
        }
    } else if (meta.isWater) {
        // 水：根据 level 计算高度（水源 0.875，流动水递减）
        int stateCount = info->maxStateId - info->minStateId + 1;
        if (stateCount >= 8) {
            int level = blockState - info->minStateId;
            if (level <= 0) level = 0;
            if (level >= 7) level = 7;
            // level 0 = 14/16, level 7 = 4/16（标准 Minecraft 水流高度）
            meta.height = (14 - level * 2) / 16.0f;
        } else {
            meta.height = 0.875f;
        }
    } else {
        meta.height = 1.0f;
    }

    // ---- 完整方块判定（用于面剔除） ----
    meta.isFullBlock = (blockState != 0 && !meta.isPlant && !meta.isLeaves && !meta.isWater && meta.height >= 1.0f);

    // ---- 纹理配置（复制 getBlockTexture 逻辑） ----
    if (meta.isGrassBlock) {
        meta.texTop = TEX_GRASS_TOP;
        meta.texSide = TEX_GRASS_SIDE;
        meta.texBottom = TEX_DIRT;
    } else if (meta.name == "dirt" || meta.name == "coarse_dirt"
               || meta.name == "rooted_dirt" || meta.name == "mud") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_DIRT;
    } else if (meta.name == "stone" || meta.name == "andesite"
               || meta.name == "diorite" || meta.name == "granite"
               || meta.name == "deepslate" || meta.name == "tuff"
               || meta.name == "calcite" || meta.name == "dripstone_block") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_STONE;
    } else if (meta.name == "cobblestone" || meta.name == "mossy_cobblestone"
               || meta.name == "stone_bricks" || meta.name == "cracked_stone_bricks"
               || meta.name == "mossy_stone_bricks") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_COBBLESTONE;
    } else if (meta.name == "oak_planks" || meta.name == "oak_stairs"
               || meta.name == "oak_slab" || meta.name == "oak_fence") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_OAK_PLANKS;
    } else if (meta.name == "oak_log" || meta.name == "oak_wood"
               || meta.name == "stripped_oak_log" || meta.name == "stripped_oak_wood") {
        meta.texTop = meta.texBottom = TEX_OAK_LOG_TOP;
        meta.texSide = TEX_OAK_LOG_SIDE;
    } else if (meta.name == "spruce_planks" || meta.name == "spruce_stairs"
               || meta.name == "spruce_slab" || meta.name == "spruce_fence") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_SPRUCE_PLANKS;
    } else if (meta.name == "spruce_log" || meta.name == "spruce_wood"
               || meta.name == "stripped_spruce_log" || meta.name == "stripped_spruce_wood") {
        meta.texTop = meta.texBottom = TEX_SPRUCE_LOG_TOP;
        meta.texSide = TEX_SPRUCE_LOG_SIDE;
    } else if (meta.name == "sand" || meta.name == "red_sand"
               || meta.name == "sandstone" || meta.name == "red_sandstone") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_SAND;
    } else if (meta.name == "gravel") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_GRAVEL;
    } else if (meta.name == "oak_leaves" || meta.name == "birch_leaves"
               || meta.name == "jungle_leaves" || meta.name == "acacia_leaves"
               || meta.name == "dark_oak_leaves" || meta.name == "azalea_leaves"
               || meta.name == "flowering_azalea_leaves") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_OAK_LEAVES;
    } else if (meta.name == "spruce_leaves") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_SPRUCE_LEAVES;
    } else if (meta.isGrassBlock) { // 雪草方块（已被 grass_block 覆盖，这里不会到达，保留以防）
        meta.texTop = meta.texSide = TEX_GRASS_BLOCK_SNOW;
        meta.texBottom = TEX_DIRT;
    } else if (meta.isSnow) {
        meta.texTop = meta.texSide = meta.texBottom = TEX_SNOW;
    } else if (meta.name == "ice" || meta.name == "packed_ice" || meta.name == "blue_ice" || meta.name == "frosted_ice") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_ICE;
    } else if (meta.isWater) {
        meta.texTop = meta.texSide = meta.texBottom = TEX_WATER;
    } else if (meta.isPlant || meta.name == "grass" || meta.name == "tall_grass"
               || meta.name == "fern" || meta.name == "large_fern") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_GRASS_PLANT;
    } else if (meta.name == "dandelion" || meta.name == "poppy" || meta.name == "blue_orchid"
               || meta.name == "allium" || meta.name == "azure_bluet" || meta.name == "oxeye_daisy"
               || meta.name == "cornflower" || meta.name == "lily_of_the_valley"
               || meta.name == "wither_rose" || meta.name == "sunflower"
               || meta.name == "lilac" || meta.name == "rose_bush" || meta.name == "peony") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_OAK_PLANKS;
    } else if (meta.name == "vine" || meta.name == "lily_pad" || meta.name == "dead_bush"
               || meta.name == "sugar_cane" || meta.name == "brown_mushroom"
               || meta.name == "red_mushroom" || meta.name == "cactus") {
        meta.texTop = meta.texSide = meta.texBottom = TEX_GRASS_TOP;
    } else {
        // 未知方块：根据 blockState ID 取模分配纹理
        int texIndex = TEX_STONE + (blockState % 10);
        if (texIndex >= TEXTURE_LAYER_COUNT) texIndex = TEX_STONE;
        meta.texTop = meta.texSide = meta.texBottom = texIndex;
    }

    return meta;
}
