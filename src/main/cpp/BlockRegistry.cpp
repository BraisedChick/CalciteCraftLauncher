#include "BlockRegistry.h"
#include "TextureAtlas.h"
#include "ClientEngine/ClientEngine.h"
#include "3rdparty/json.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <android/log.h>

#define LOG_TAG "BlockRegistry"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using json = nlohmann::json;

bool BlockRegistry::loadFromJson(const std::string& jsonStr) {
    LOGI("Loading blocks from JSON string (%zu bytes)", jsonStr.size());

    if (jsonStr.empty()) {
        LOGE("blocks.json content is empty!");
        return false;
    }

    json root;
    try {
        root = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        LOGE("JSON parse error in blocks.json: %s", e.what());
        return false;
    }

    if (!root.is_array()) {
        LOGE("blocks.json root is not an array");
        return false;
    }

    int blockCount = 0;

    for (const auto& item : root) {
        BlockInfo info;
        info.id = item.value("id", -1);
        info.name = item.value("name", "");
        // 去掉 "minecraft:" 前缀
        {
            size_t mcPos = info.name.find(':');
            if (mcPos != std::string::npos) {
                info.name = info.name.substr(mcPos + 1);
            }
        }
        info.displayName = item.value("displayName", "");
        info.minStateId = item.value("minStateId", -1);
        info.maxStateId = item.value("maxStateId", -1);
        info.defaultState = item.value("defaultState", -1);
        info.hardness = (item.contains("hardness") && !item["hardness"].is_null())
            ? item["hardness"].get<float>()
            : 0.0f;
        info.material = item.value("material", "");
        info.hasHarvestTools = item.contains("harvestTools");

        // 解析 states 数组
        if (item.contains("states")) {
            const auto& states = item["states"];
            if (states.is_array()) {
                for (const auto& state : states) {
                    BlockInfo::StateProperty prop;
                    prop.name = state.value("name", "");
                    prop.type = state.value("type", "");

                    // 解析 values 数组
                    if (state.contains("values") && state["values"].is_array()) {
                        for (const auto& val : state["values"]) {
                            if (val.is_string()) {
                                prop.values.push_back(val.get<std::string>());
                            }
                        }
                    } else if (prop.type == "bool") {
                        prop.values = {"true", "false"};
                    } else if (prop.type == "int") {
                        int numValues = state.value("num_values", 0);
                        if (numValues > 0) {
                            for (int i = 0; i < numValues; i++) {
                                prop.values.push_back(std::to_string(i));
                            }
                        }
                    }

                    if (!prop.name.empty()) {
                        info.stateProperties.push_back(std::move(prop));
                    }
                }
            }
        }

        // 验证数据有效性
        if (info.name.empty() || info.minStateId < 0) {
            continue;
        }

        // 存储方块信息
        size_t blockIndex = blocks.size();
        blocks.push_back(info);

        // registry ID → 名称（物品栏用）
        idToName[info.id] = info.name;

        // 构建 blockState ID → BlockInfo 映射
        for (int32_t stateId = info.minStateId; stateId <= info.maxStateId; ++stateId) {
            stateToBlock[stateId] = blockIndex;
        }

        blockCount++;
        if (blockCount % 100 == 0) {
            LOGI("Parsed %d blocks...", blockCount);
        }
    }

    loaded = true;
    LOGI("Successfully loaded %d blocks with %zu state mappings",
         blockCount, stateToBlock.size());

    for (int i = 0; i < std::min(10, blockCount); i++) {
        LOGI("  Block[%d]: id=%d name='%s'", i, blocks[i].id, blocks[i].name.c_str());
    }

    return true;
}

bool BlockRegistry::loadItems(const std::string& jsonStr) {
    if (jsonStr.empty()) {
        LOGE("items.json content is empty");
        return false;
    }

    json root;
    try {
        root = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        LOGE("JSON parse error in items.json: %s", e.what());
        return false;
    }

    if (!root.is_object()) {
        LOGE("items.json root is not an object");
        return false;
    }

    LOGI("Loaded items.json (%zu bytes)", jsonStr.size());

    int itemCount = 0;

    for (auto it = root.begin(); it != root.end(); ++it) {
        const std::string& fullName = it.key();
        // key 格式: "minecraft:xxx"
        size_t colonPos = fullName.find(':');
        if (colonPos == std::string::npos) continue;
        std::string shortName = fullName.substr(colonPos + 1);
        if (shortName.empty()) continue;

        int32_t itemId = it->value("id", -1);
        if (itemId < 0) continue;

        idToName[itemId] = shortName;
        itemCount++;
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

// ===== 以下为其余未修改的方法 =====

void BlockRegistry::precomputeAll() {
    LOGI("Precomputing metadata for %zu block states...", stateToBlock.size());
    metadataCache.reserve(stateToBlock.size());
    for (const auto& [stateId, _] : stateToBlock) {
        metadataCache[stateId] = computeMetadata(stateId);
    }
    LOGI("Precomputed metadata for %zu block states", metadataCache.size());
}

const BlockMetadata& BlockRegistry::getBlockMetadata(int32_t blockState) const {
    auto it = metadataCache.find(blockState);
    if (it != metadataCache.end()) return it->second;
    static BlockMetadata emptyMeta{};
    return emptyMeta;
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
    meta.hardness = info->hardness;
    meta.requiresCorrectTool = info->hasHarvestTools;
    meta.material = info->material;

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
        || meta.name == "lilac" || meta.name == "rose_bush" || meta.name == "peony"
        || meta.name == "wheat" || meta.name == "carrots" || meta.name == "potatoes"
        || meta.name == "beetroots" || meta.name == "nether_wart");

    meta.isWater = (meta.name == "water");

    {
        auto hasSuffix = [&](const std::string& suffix) {
            if (suffix.size() > meta.name.size()) return false;
            return meta.name.compare(meta.name.size() - suffix.size(), suffix.size(), suffix) == 0;
        };
        meta.isNoCollision = (
            meta.name == "torch" || meta.name == "wall_torch" ||
            meta.name == "soul_torch" || meta.name == "soul_wall_torch" ||
            meta.name == "redstone_torch" || meta.name == "redstone_wall_torch" ||
            meta.name == "fire" || meta.name == "soul_fire" ||
            meta.name == "redstone_wire" || meta.name == "tripwire" ||
            meta.name == "tripwire_hook" || meta.name == "lever" ||
            meta.name == "repeater" || meta.name == "comparator" ||
            meta.name == "nether_portal" || meta.name == "end_portal" ||
            meta.name == "cobweb" ||
            hasSuffix("_button") ||
            hasSuffix("_pressure_plate") ||
            hasSuffix("_rail") ||
            hasSuffix("_sign") || hasSuffix("_wall_sign") ||
            hasSuffix("_banner") || hasSuffix("_wall_banner")
        );
    }

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
        int stateCount = info->maxStateId - info->minStateId + 1;
        if (stateCount >= 8) {
            int level = blockState - info->minStateId;
            if (level <= 0) level = 0;
            if (level >= 7) level = 7;
            meta.height = (14 - level * 2) / 16.0f;
        } else {
            meta.height = 0.875f;
        }
    } else {
        meta.height = 1.0f;
    }

    meta.isFullBlock = false;
    if (blockState != 0 && !meta.isAir && !meta.isWater && !meta.isLeaves && !meta.isPlant) {
        auto* atlas = ClientEngine::getInstance()->getTextureAtlas();
        if (atlas && atlas->isInitialized()) {
            const auto* model = atlas->getBlockModel(meta.name);
            if ((!model || model->elements.empty()) && meta.minStateId >= 0) {
                const auto* variant = atlas->getBlockStateVariant(
                    meta.name, blockState, meta.minStateId);
                if (variant) {
                    model = atlas->getBlockModel(variant->modelName());
                }
            }
            if (model && !model->elements.empty()) {
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
                meta.isFullBlock = true;
            }
        } else {
            meta.isFullBlock = true;
        }
    }

    if (meta.name.find("glass") != std::string::npos ||
        meta.name == "ice" ||
        meta.name.find("leaves") != std::string::npos ||
        meta.name == "slime_block" ||
        meta.name == "honey_block") {
        meta.isOpaque = false;
    }

    auto* atlas = ClientEngine::getInstance()->getTextureAtlas();
    if (atlas && atlas->isInitialized()) {
        auto tex = atlas->getBlockTexture(meta.name);
        meta.texTop = tex.top;
        meta.texSide = tex.side;
        meta.texBottom = tex.bottom;
    } else {
        meta.texTop = meta.texSide = meta.texBottom = 0;
    }

    return meta;
}

std::string BlockRegistry::getItemName(int32_t itemId) const {
    auto it = idToName.find(itemId);
    if (it != idToName.end()) return it->second;
    return "";
}

const BlockInfo* BlockRegistry::getBlockInfoByName(const std::string& name) const {
    for (const auto& block : blocks) {
        if (block.name == name) return &block;
    }
    return nullptr;
}
