#include "BlockRegistry.h"
#include "TextureAtlas.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <android/log.h>

#define LOG_TAG "BlockRegistry"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool BlockRegistry::loadFromJson(const std::string& json) {
    LOGI("Loading blocks from JSON string (%zu bytes)", json.size());

    if (json.empty()) {
        LOGE("blocks.json content is empty!");
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

        // registry ID → 名称（物品栏用）
        idToName[info.id] = info.name;
        
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

    // 打印前 10 个方块名用于调试
    for (int i = 0; i < std::min(10, blockCount); i++) {
        LOGI("  Block[%d]: id=%d name='%s'", i, blocks[i].id, blocks[i].name.c_str());
    }

    return true;
}

bool BlockRegistry::loadItems(const std::string& json) {
    if (json.empty()) {
        LOGE("items.json content is empty");
        return false;
    }

    LOGI("Loaded items.json (%zu bytes)", json.size());

    // 解析格式：{"minecraft:name": {"id": value}, ...}
    size_t pos = 0;
    int itemCount = 0;
    size_t keyStart;

    while ((keyStart = json.find("\"minecraft:", pos)) != std::string::npos) {
        size_t keyEnd = json.find("\"", keyStart + 1);
        if (keyEnd == std::string::npos) break;

        std::string fullName = json.substr(keyStart + 1, keyEnd - keyStart - 1);
        std::string shortName = fullName.substr(10); // 去掉 "minecraft:" 前缀

        // 查找 "id": value
        size_t idPos = json.find("\"id\"", keyEnd);
        if (idPos == std::string::npos || idPos > keyEnd + 60) break;

        size_t colonPos = json.find(":", idPos);
        if (colonPos == std::string::npos) break;

        // 跳过空格，读取数字
        size_t numStart = colonPos + 1;
        while (numStart < json.size() && (json[numStart] == ' ' || json[numStart] == '\t')) numStart++;
        if (numStart >= json.size()) break;

        char* endPtr = nullptr;
        int32_t itemId = (int32_t)strtol(json.c_str() + numStart, &endPtr, 10);
        if (endPtr == json.c_str() + numStart) break; // 没读到数字

        // items.json 的物品名称覆盖 blocks.json 的同 ID 条目（协议注册 ID 相同）
        idToName[itemId] = shortName;
        itemCount++;

        pos = (size_t)(endPtr - json.c_str());
    }

    LOGI("Loaded %d item name mappings from items.json (total idToName: %zu)",
         itemCount, idToName.size());
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
    meta.minStateId = info->minStateId;

    // ---- 方块类型判断 ----
    meta.isAir = (meta.name == "air" || meta.name == "cave_air" || meta.name == "void_air");
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

    // ---- 完整方块判定（基于模型几何，用于面剔除） ----
    meta.isFullBlock = false;
    if (blockState != 0 && !meta.isAir && !meta.isWater && !meta.isLeaves) {
        auto& atlas = TextureAtlas::getInstance();
        if (atlas.isInitialized()) {
            const auto* model = atlas.getBlockModel(meta.name);
            if (model && !model->elements.empty()) {
                // 检查是否有元素完整覆盖 16x16x16 且 6 个面都有 cullface
                for (const auto& elem : model->elements) {
                    if (elem.from[0] <= 0.001f && elem.from[1] <= 0.001f && elem.from[2] <= 0.001f &&
                        elem.to[0] >= 15.999f && elem.to[1] >= 15.999f && elem.to[2] >= 15.999f) {
                        bool allCull = true;
                        for (int i = 0; i < 6; i++) {
                            if (!elem.hasFaces[i] || elem.faces[i].cullface < 0) { allCull = false; break; }
                        }
                        if (allCull) { meta.isFullBlock = true; break; }
                    }
                }
            } else {
                // 无模型数据 → 旧立方体回退 → 完整方块
                meta.isFullBlock = true;
            }
        } else {
            meta.isFullBlock = true;
        }
    }

    // ---- 纹理配置（从 TextureAtlas 动态解析） ----
    auto& atlas = TextureAtlas::getInstance();
    if (atlas.isInitialized()) {
        auto tex = atlas.getBlockTexture(meta.name);
        meta.texTop = tex.top;
        meta.texSide = tex.side;
        meta.texBottom = tex.bottom;
    } else {
        // TextureAtlas 还没初始化，使用默认值
        meta.texTop = meta.texSide = meta.texBottom = 0;
    }

    return meta;
}

std::string BlockRegistry::getItemName(int32_t itemId) const {
    auto it = idToName.find(itemId);
    if (it != idToName.end()) return it->second;
    return "";
}
