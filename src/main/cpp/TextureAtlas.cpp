#include "TextureAtlas.h"
#include "TextureLoader.h"
#include "BlockRegistry.h"
#include "3rdparty/json.hpp"
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <unordered_set>

#define LOG_TAG "TextureAtlas"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

using json = nlohmann::json;

TextureAtlas& TextureAtlas::getInstance() {
    static TextureAtlas instance;
    return instance;
}

// ============================================================
// 辅助函数
// ============================================================

static std::string normalizeTexturePath(const std::string& path) {
    size_t colonPos = path.find(':');
    if (colonPos != std::string::npos) {
        return path.substr(colonPos + 1);
    }
    return path;
}

FaceDir TextureAtlas::faceDirFromString(const std::string& dir) {
    if (dir == "down")  return FACE_DOWN;
    if (dir == "up")    return FACE_UP;
    if (dir == "north") return FACE_NORTH;
    if (dir == "south") return FACE_SOUTH;
    if (dir == "west")  return FACE_WEST;
    if (dir == "east")  return FACE_EAST;
    return FACE_NONE;
}

// ============================================================
// 模型纹理解析（递归解析 parent 链）
// ============================================================

TextureAtlas::TextureMap TextureAtlas::resolveModelTextures(
    const std::string& modelName,
    const json& j,
    std::unordered_map<std::string, TextureMap>& cache,
    const std::unordered_map<std::string, json>* modelContentCache) {

    {
        auto it = cache.find(modelName);
        if (it != cache.end()) return it->second;
    }

    if (j.is_null()) {
        cache[modelName] = {};
        return {};
    }

    // 提取 textures 对象
    TextureMap ownTextures;
    if (j.contains("textures") && j["textures"].is_object()) {
        for (auto it = j["textures"].begin(); it != j["textures"].end(); ++it) {
            if (it->is_string()) {
                ownTextures[it.key()] = it->get<std::string>();
            }
        }
    }

    std::string parent = j.value("parent", "");
    TextureMap result;

    if (!parent.empty()) {
        std::string parentModel = normalizeTexturePath(parent);

        json parentJson;
        if (modelContentCache) {
            auto it = modelContentCache->find(parentModel);
            if (it != modelContentCache->end()) {
                parentJson = it->second;
            }
        }
        if (parentJson.is_null()) {
            std::string raw = TextureLoader::readTextFromZip("models/" + parentModel + ".json");
            if (!raw.empty()) {
                try { parentJson = json::parse(raw); } catch (...) {}
            }
        }

        auto parentResolved = resolveModelTextures(parentModel, parentJson, cache, modelContentCache);
        result = std::move(parentResolved);

        for (const auto& [key, value] : ownTextures) {
            if (value == "*") {
                continue;
            } else if (!value.empty() && value[0] == '#') {
                std::string varName = value.substr(1);
                auto varIt = result.find(varName);
                if (varIt != result.end()) {
                    result[key] = varIt->second;
                } else {
                    result[key] = value;
                }
            } else {
                result[key] = value;
            }
        }

        {
            bool changed = true;
            while (changed) {
                changed = false;
                for (auto& [k, v] : result) {
                    if (!v.empty() && v[0] == '#') {
                        std::string varName = v.substr(1);
                        auto it = result.find(varName);
                        if (it != result.end() && !it->second.empty() && it->second[0] != '#') {
                            v = it->second;
                            changed = true;
                        }
                    }
                }
            }
        }
    } else {
        result = std::move(ownTextures);
    }

    cache[modelName] = result;
    return result;
}


// ============================================================
// 纹理路径 → 文件名（"block/stone" → "stone.png"）
// ============================================================
std::string TextureAtlas::texturePathToFilename(const std::string& texturePath) {
    std::string path = texturePath;
    size_t colonPos = path.find(':');
    if (colonPos != std::string::npos) {
        path = path.substr(colonPos + 1);
    }
    if (path.find("block/") == 0) {
        path = path.substr(6);
    }
    return path + ".png";
}

// ============================================================
// 确保纹理路径存在，返回图层索引
// ============================================================
int TextureAtlas::ensureTexture(const std::string& texturePath) {
    if (texturePath.empty()) return 0;

    std::string normalized = texturePath;
    size_t colonPos = normalized.find(':');
    if (colonPos != std::string::npos) {
        normalized = normalized.substr(colonPos + 1);
    }

    auto it = texturePathToLayer.find(normalized);
    if (it != texturePathToLayer.end()) return it->second;

    int index = static_cast<int>(textureList.size());
    textureList.push_back(texturePathToFilename(normalized));
    texturePathToLayer[normalized] = index;
    return index;
}

// ============================================================
// 模型元素解析（ELEMENTS 解析）— 全部改用 json 对象参数
// ============================================================

// 从 json 数组中提取 float 值
static bool extractFloatArray(const json& j, float* out, size_t count) {
    if (!j.is_array() || j.size() < count) return false;
    for (size_t i = 0; i < count; i++) {
        out[i] = j[i].get<float>();
    }
    return true;
}

void TextureAtlas::parseElementRotation(ModelElementData& element, const json& rot) {
    if (rot.is_null()) return;

    if (rot.contains("origin") && rot["origin"].is_array()) {
        extractFloatArray(rot["origin"], element.rotation.origin, 3);
    }

    std::string axisStr = rot.value("axis", "");
    if (axisStr == "x") element.rotation.axis = 0;
    else if (axisStr == "y") element.rotation.axis = 1;
    else if (axisStr == "z") element.rotation.axis = 2;

    element.rotation.angle = rot.value("angle", 0.0f);
    element.rotation.rescale = rot.value("rescale", false);
}

void TextureAtlas::parseElementFaces(ModelElementData& element,
                                     const json& faces,
                                     const TextureMap& resolvedTextures,
                                     const std::unordered_map<std::string, int>& pathToLayer) {
    static const char* faceNames[] = {"down", "up", "north", "south", "west", "east"};
    for (const char* faceName : faceNames) {
        if (!faces.contains(faceName)) continue;

        FaceDir dir = faceDirFromString(faceName);
        if (dir == FACE_NONE) continue;

        const json& fc = faces[faceName];
        if (!fc.is_object()) continue;

        ModelFaceData& face = element.faces[dir];
        element.hasFaces[dir] = true;
        face.shade = element.shade;

        // UV
        if (fc.contains("uv") && fc["uv"].is_array()) {
            extractFloatArray(fc["uv"], face.uv, 4);
        }

        // texture
        std::string texRef = fc.value("texture", "");
        if (!texRef.empty() && texRef[0] == '#') {
            std::string varName = texRef.substr(1);
            auto varIt = resolvedTextures.find(varName);
            if (varIt != resolvedTextures.end() && !varIt->second.empty()) {
                std::string normalized = normalizeTexturePath(varIt->second);
                auto layerIt = pathToLayer.find(normalized);
                if (layerIt != pathToLayer.end()) {
                    face.textureLayer = layerIt->second;
                } else {
                    face.textureLayer = 0;
                }
            }
        }

        // cullface
        std::string cullStr = fc.value("cullface", "");
        if (!cullStr.empty()) {
            face.cullface = (int8_t)faceDirFromString(cullStr);
        }

        // tintindex
        face.tintindex = (int8_t)fc.value("tintindex", -1);

        // UV 旋转 (0/90/180/270)
        face.rotation = (uint16_t)fc.value("rotation", 0);
    }
}

void TextureAtlas::parseSingleElement(ModelElementData& element,
                                      const json& elem,
                                      const TextureMap& resolvedTextures,
                                      const std::unordered_map<std::string, int>& pathToLayer) {
    // from / to
    if (elem.contains("from") && elem["from"].is_array()) {
        extractFloatArray(elem["from"], element.from, 3);
    }
    if (elem.contains("to") && elem["to"].is_array()) {
        extractFloatArray(elem["to"], element.to, 3);
    }

    // shade
    element.shade = elem.value("shade", true);

    // faces
    if (elem.contains("faces") && elem["faces"].is_object()) {
        parseElementFaces(element, elem["faces"], resolvedTextures, pathToLayer);
    }

    // rotation
    if (elem.contains("rotation") && elem["rotation"].is_object()) {
        parseElementRotation(element, elem["rotation"]);
    }
}

void TextureAtlas::jsonExtractElement(std::vector<ModelElementData>& elements,
                                      const json& modelJson,
                                      const TextureMap& resolvedTextures,
                                      const std::unordered_map<std::string, int>& pathToLayer) {
    if (!modelJson.contains("elements") || !modelJson["elements"].is_array()) return;

    for (const auto& elemJson : modelJson["elements"]) {
        if (!elemJson.is_object()) continue;
        ModelElementData element;
        parseSingleElement(element, elemJson, resolvedTextures, pathToLayer);
        elements.push_back(std::move(element));
    }
}


bool TextureAtlas::resolveBlockModelElements(
    const std::string& blockName,
    const json& j,
    std::unordered_map<std::string, bool>& elementCache,
    const std::unordered_map<std::string, json>* modelContentCache) {

    {
        auto it = elementCache.find(blockName);
        if (it != elementCache.end()) return it->second;
        elementCache[blockName] = false;
    }

    if (j.is_null()) {
        elementCache[blockName] = false;
        return false;
    }

    bool hasOwnElements = j.contains("elements") && j["elements"].is_array();
    std::string parent = j.value("parent", "");
    bool parentHasElements = false;

    if (!parent.empty()) {
        std::string parentModel = normalizeTexturePath(parent);

        json parentJson;
        if (modelContentCache) {
            auto it = modelContentCache->find(parentModel);
            if (it != modelContentCache->end()) {
                parentJson = it->second;
            }
        }
        if (parentJson.is_null()) {
            std::string raw = TextureLoader::readTextFromZip("models/" + parentModel + ".json");
            if (!raw.empty()) {
                try { parentJson = json::parse(raw); } catch (...) {}
            }
        }

        parentHasElements = resolveBlockModelElements(parentModel, parentJson, elementCache, modelContentCache);
    }

    if (!hasOwnElements && !parentHasElements) {
        elementCache[blockName] = false;
        return false;
    }

    // 重新解析这个 block 的 texture map
    std::unordered_map<std::string, TextureMap> tempCache;
    auto resolvedTextures = resolveModelTextures(blockName, j, tempCache, modelContentCache);

    ResolvedBlockModel& model = blockModelCache[blockName];

    if (hasOwnElements) {
        jsonExtractElement(model.elements, j, resolvedTextures, texturePathToLayer);
    } else {
        // 继承父的 elements
        std::string parentModel = normalizeTexturePath(j.value("parent", ""));
        auto parentIt = blockModelCache.find(parentModel);
        // 获取父的原始 JSON 重新解析（用当前的 resolvedTextures）
        json parentJson;
        if (modelContentCache) {
            auto it = modelContentCache->find(parentModel);
            if (it != modelContentCache->end()) parentJson = it->second;
        }
        if (parentJson.is_null()) {
            std::string raw = TextureLoader::readTextFromZip("models/" + parentModel + ".json");
            if (!raw.empty()) {
                try { parentJson = json::parse(raw); } catch (...) {}
            }
        }
        if (!parentJson.is_null()) {
            model.elements.clear();
            jsonExtractElement(model.elements, parentJson, resolvedTextures, texturePathToLayer);
        }
    }

    elementCache[blockName] = !model.elements.empty();
    return !model.elements.empty();
}

const ResolvedBlockModel* TextureAtlas::getBlockModel(const std::string& blockName) const {
    auto it = blockModelCache.find(blockName);
    if (it != blockModelCache.end() && !it->second.elements.empty()) {
        return &it->second;
    }
    return nullptr;
}

// ============================================================
// 从纹理路径集合中按优先级取第一个有效值
// ============================================================
static std::string getFirstTexture(const std::unordered_map<std::string, std::string>& map,
                                    std::initializer_list<std::string> keys) {
    for (const auto& key : keys) {
        auto it = map.find(key);
        if (it != map.end() && !it->second.empty() && it->second[0] != '#') {
            return it->second;
        }
    }
    return "";
}


// ============================================================
// 初始化：遍历所有模型 JSON，构建纹理索引
// ============================================================
bool TextureAtlas::initialize(std::function<void(float, const char*)> progressCallback) {
    std::lock_guard<std::mutex> lock(mutex);

    if (initialized) return true;

    LOGI("Initializing TextureAtlas from models/block/*.json...");

    if (progressCallback) progressCallback(0.0f, "\u8bfb\u53d6\u6a21\u578b\u6587\u4ef6...");

    // 1. 读取所有模型 JSON
    auto modelFiles = TextureLoader::readAllTextFromZip("models/block/");
    if (modelFiles.empty()) {
        LOGE("No model files found in ZIP at models/block/");
        ensureTexture("block/stone");
        ensureTexture("block/dirt");
        ensureTexture("block/grass_block_top");
        ensureTexture("block/grass_block_side");
        ensureTexture("block/grass_block_side_overlay");

        grassSideOverlayLayer = getLayerByTexturePath("block/grass_block_side_overlay");
        grassBlockSnowLayer = getLayerByTexturePath("block/grass_block_top");
        grassSideLayer = getLayerByTexturePath("block/grass_block_side");
        initialized = true;
        LOGI("TextureAtlas initialized with %d fallback textures", getLayerCount());
        return true;
    }

    LOGI("Found %zu model files in ZIP", modelFiles.size());

    // 2. 构建模型内容缓存（blockName → json 对象），一次 parse 避免重复解析
    std::unordered_map<std::string, json> modelContentCache;
    modelContentCache.reserve(modelFiles.size());
    for (const auto& [entryPath, content] : modelFiles) {
        std::string blockName = entryPath;
        size_t slashPos = blockName.rfind('/');
        if (slashPos != std::string::npos) blockName = blockName.substr(slashPos + 1);
        size_t extPos = blockName.rfind(".json");
        if (extPos != std::string::npos) blockName = blockName.substr(0, extPos);
        if (!blockName.empty()) {
            try {
                modelContentCache[blockName] = json::parse(content);
            } catch (...) {
                LOGW("Failed to parse JSON for model: %s", blockName.c_str());
            }
        }
    }
    LOGI("Cached %zu model contents", modelContentCache.size());

    // 3. 解析模型缓存
    std::unordered_map<std::string, TextureMap> resolutionCache;

    // 4. 遍历所有 block 模型
    int parsedCount = 0;
    int totalModels = static_cast<int>(modelContentCache.size());
    for (const auto& [blockName, j] : modelContentCache) {
        auto resolved = resolveModelTextures(blockName, j, resolutionCache, &modelContentCache);
        if (resolved.empty()) continue;

        ModelTextures mt;
        mt.top = getFirstTexture(resolved, {"up", "top", "all", "end", "particle", "platform"});
        mt.side = getFirstTexture(resolved, {"north", "south", "east", "west", "side", "all", "end", "particle", "inside"});
        mt.bottom = getFirstTexture(resolved, {"down", "bottom", "all", "end", "particle"});

        if (mt.top.empty())    mt.top = "block/stone";
        if (mt.side.empty())   mt.side = "block/stone";
        if (mt.bottom.empty()) mt.bottom = "block/stone";

        blockTextureMap[blockName] = mt;

        for (const auto& [varName, texPath] : resolved) {
            if (!texPath.empty()) ensureTexture(texPath);
        }

        // 解析模型 elements
        if (j.contains("elements") && j["elements"].is_array()) {
            jsonExtractElement(blockModelCache[blockName].elements, j, resolved, texturePathToLayer);
        } else {
            std::string parent = j.value("parent", "");
            if (!parent.empty()) {
                std::string parentModel = normalizeTexturePath(parent);
                json parentJson;
                auto cacheIt = modelContentCache.find(parentModel);
                if (cacheIt != modelContentCache.end()) {
                    parentJson = cacheIt->second;
                } else {
                    std::string raw = TextureLoader::readTextFromZip("models/" + parentModel + ".json");
                    if (!raw.empty()) { try { parentJson = json::parse(raw); } catch (...) {} }
                }
                std::string currentName = parentModel;
                json currentJson = parentJson;
                while (!currentJson.is_null()) {
                    if (currentJson.contains("elements") && currentJson["elements"].is_array()) {
                        jsonExtractElement(blockModelCache[blockName].elements, currentJson, resolved, texturePathToLayer);
                        break;
                    }
                    std::string nextParent = currentJson.value("parent", "");
                    if (nextParent.empty()) break;
                    std::string nextName = normalizeTexturePath(nextParent);
                    if (nextName == currentName) break;
                    auto nextIt = modelContentCache.find(nextName);
                    if (nextIt != modelContentCache.end()) {
                        currentJson = nextIt->second;
                    } else {
                        std::string raw = TextureLoader::readTextFromZip("models/" + nextName + ".json");
                        if (!raw.empty()) { try { currentJson = json::parse(raw); } catch (...) { currentJson = nullptr; } }
                        else { currentJson = nullptr; }
                    }
                    currentName = nextName;
                }
            }
        }

        parsedCount++;
        if (progressCallback && (parsedCount % 200 == 0 || parsedCount == totalModels)) {
            float p = 0.01f + 0.04f * (float)parsedCount / (float)totalModels;
            char buf[64];
            snprintf(buf, sizeof(buf), "\u89e3\u6790\u65b9\u5757\u6a21\u578b %d/%d", parsedCount, totalModels);
            progressCallback(p, buf);
        }
    }

    if (progressCallback) {
        float p = 0.05f;
        char buf[64];
        snprintf(buf, sizeof(buf), "\u89e3\u6790\u65b9\u5757\u6a21\u578b %d/%d", parsedCount, totalModels);
        progressCallback(p, buf);
    }

    LOGI("Parsed %d/%zu block models", parsedCount, modelFiles.size());

    {
        int elemCount = 0;
        for (const auto& [name, model] : blockModelCache) {
            if (!model.elements.empty()) elemCount++;
        }
        LOGI("Resolved %d block models with geometry elements", elemCount);
    }

    // 4.5 加载 blockstate JSON
    {
        auto blockstateFiles = TextureLoader::readAllTextFromZip("blockstates/");
        int bsCount = 0;
        for (const auto& [entryPath, content] : blockstateFiles) {
            std::string blockName = entryPath;
            size_t slash = blockName.rfind('/');
            if (slash != std::string::npos) blockName = blockName.substr(slash + 1);
            size_t dot = blockName.rfind(".json");
            if (dot != std::string::npos) blockName = blockName.substr(0, dot);
            if (!blockName.empty()) {
                json j;
                try { j = json::parse(content); } catch (...) { continue; }
                parseBlockState(blockName, j);
                bsCount++;
            }
        }
        LOGI("Parsed %d blockstate files", bsCount);
    }

    // 4.6 加载 models/item/*.json
    {
        auto itemModelFiles = TextureLoader::readAllTextFromZip("models/item/");
        int itemCount = 0;
        for (const auto& [entryPath, content] : itemModelFiles) {
            std::string itemName = entryPath;
            size_t slash = itemName.rfind('/');
            if (slash != std::string::npos) itemName = itemName.substr(slash + 1);
            size_t dot = itemName.rfind(".json");
            if (dot != std::string::npos) itemName = itemName.substr(0, dot);
            if (!itemName.empty()) {
                json j;
                try { j = json::parse(content); } catch (...) { continue; }
                std::string parent = j.value("parent", "");
                if (!parent.empty()) {
                    size_t colonPos = parent.find(':');
                    if (colonPos != std::string::npos) parent = parent.substr(colonPos + 1);
                    if (parent.find("block/") == 0) parent = parent.substr(6);
                    itemModelCache[itemName] = parent;
                    itemCount++;
                }
            }
        }
        LOGI("Parsed %d item model references", itemCount);
    }

    // 5. 强制加入特殊纹理
    ensureTexture("block/grass_block_side_overlay");
    ensureTexture("block/snow");

    // 6. 加载方块破坏动画纹理
    for (int i = 0; i < 10; i++) {
        ensureTexture("block/destroy_stage_" + std::to_string(i));
    }

    // 7. 缓存特殊纹理索引
    auto findLayer = [this](const std::string& path) {
        auto it = texturePathToLayer.find(path);
        return it != texturePathToLayer.end() ? it->second : -1;
    };
    grassSideOverlayLayer = findLayer("block/grass_block_side_overlay");
    grassBlockSnowLayer = findLayer("block/grass_block_top");
    grassSideLayer = findLayer("block/grass_block_side");
    for (int i = 0; i < 10; i++) {
        destroyStageLayers[i] = findLayer("block/destroy_stage_" + std::to_string(i));
    }

    if (grassSideOverlayLayer < 0) grassSideOverlayLayer = findLayer("block/stone");
    if (grassBlockSnowLayer < 0)   grassBlockSnowLayer = findLayer("block/stone");
    if (grassSideLayer < 0)        grassSideLayer = findLayer("block/stone");

    initialized = true;
    LOGI("TextureAtlas initialized: %d texture layers, %d block mappings",
         getLayerCount(), (int)blockTextureMap.size());

    {
        int count = 0;
        for (const auto& [key, val] : blockTextureMap) {
            LOGI("  BlockMap[%s] = top:%s side:%s bottom:%s", key.c_str(),
                 val.top.c_str(), val.side.c_str(), val.bottom.c_str());
            if (++count >= 20) break;
        }
    }

    for (int i = 0; i < std::min(20, getLayerCount()); i++) {
        LOGI("  Layer %d: %s", i, textureList[i].c_str());
    }

    return true;
}


// ============================================================
// 查询方块纹理
// ============================================================
BlockTextureConfig TextureAtlas::getBlockTexture(const std::string& blockName) const {
    if (!initialized) return {0, 0, 0};
    std::lock_guard<std::mutex> lock(mutex);

    auto it = blockTextureMap.find(blockName);
    if (it == blockTextureMap.end()) {
        static std::atomic<int> debugMissCount{0};
        int miss = debugMissCount.fetch_add(1);
        if (miss < 10) {
            LOGI("getBlockTexture: '%s' not in blockTextureMap (total misses: %d)", blockName.c_str(), miss);
        } else if (miss == 10) {
            LOGI("getBlockTexture: ... (suppressing further miss logs)");
        }

        std::string heuristic = "block/" + blockName;
        auto heurIt = texturePathToLayer.find(heuristic);
        if (heurIt != texturePathToLayer.end()) {
            int layer = heurIt->second;
            return {layer, layer, layer};
        }

        auto stoneIt = texturePathToLayer.find("block/stone");
        int stoneLayer = (stoneIt != texturePathToLayer.end()) ? stoneIt->second : 0;
        if (stoneLayer < 0) stoneLayer = 0;
        return {stoneLayer, stoneLayer, stoneLayer};
    }

    auto normalizePath = [](const std::string& path) -> std::string {
        size_t colonPos = path.find(':');
        if (colonPos != std::string::npos) return path.substr(colonPos + 1);
        return path;
    };

    auto topIt = texturePathToLayer.find(normalizePath(it->second.top));
    auto sideIt = texturePathToLayer.find(normalizePath(it->second.side));
    auto bottomIt = texturePathToLayer.find(normalizePath(it->second.bottom));

    int top = (topIt != texturePathToLayer.end()) ? topIt->second : 0;
    int side = (sideIt != texturePathToLayer.end()) ? sideIt->second : 0;
    int bottom = (bottomIt != texturePathToLayer.end()) ? bottomIt->second : 0;

    return {top, side, bottom};
}

std::string TextureAtlas::getTextureFileName(int layer) const {
    if (layer < 0 || layer >= static_cast<int>(textureList.size())) {
        return "stone.png";
    }
    return textureList[layer];
}

void TextureAtlas::getPlaceholderColor(int /*layer*/, uint8_t& r, uint8_t& g, uint8_t& b) const {
    r = 0xAA; g = 0x44; b = 0xAA;
}

int TextureAtlas::getLayerByTexturePath(const std::string& texturePath) const {
    std::string normalized = texturePath;
    size_t colonPos = normalized.find(':');
    if (colonPos != std::string::npos) normalized = normalized.substr(colonPos + 1);
    std::lock_guard<std::mutex> lock(mutex);
    auto it = texturePathToLayer.find(normalized);
    return (it != texturePathToLayer.end()) ? it->second : -1;
}

int TextureAtlas::getGrassSideOverlayLayer() const { return grassSideOverlayLayer; }
int TextureAtlas::getGrassBlockSnowLayer() const    { return grassBlockSnowLayer; }
int TextureAtlas::getGrassSideLayer() const         { return grassSideLayer; }


// ============================================================
// Blockstate 变体解析
// ============================================================

std::vector<std::pair<std::string, std::string>> TextureAtlas::parseVariantKey(const std::string& key) {
    std::vector<std::pair<std::string, std::string>> result;
    if (key.empty()) return result;

    size_t pos = 0;
    while (pos < key.size()) {
        size_t eq = key.find('=', pos);
        if (eq == std::string::npos) break;
        std::string propName = key.substr(pos, eq - pos);
        size_t comma = key.find(',', eq + 1);
        std::string propVal;
        if (comma == std::string::npos) {
            propVal = key.substr(eq + 1);
            pos = key.size();
        } else {
            propVal = key.substr(eq + 1, comma - eq - 1);
            pos = comma + 1;
        }
        result.emplace_back(propName, propVal);
    }
    std::sort(result.begin(), result.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    return result;
}

void TextureAtlas::parseBlockState(const std::string& blockName, const json& j) {
    if (!j.contains("variants") || !j["variants"].is_object()) {
        parseMultipart(blockName, j);
        return;
    }

    const json& variants = j["variants"];

    // 第一遍：解析所有变体
    struct ParsedEntry {
        std::vector<std::pair<std::string, std::string>> props;
        BlockStateVariant bsv;
    };
    std::vector<ParsedEntry> entries;
    std::unordered_map<std::string, std::unordered_set<std::string>> propValueSet;
    bool hasSimpleVariant = false;

    for (auto it = variants.begin(); it != variants.end(); ++it) {
        const std::string& variantKey = it.key();
        const json& val = it.value();

        // 解析值（可能是单个对象 {model:..., x:..., y:...} 或数组 [{...}, {...}]）
        std::vector<json> models;
        if (val.is_array()) {
            for (const auto& m : val) {
                if (m.is_object()) models.push_back(m);
            }
        } else if (val.is_object()) {
            models.push_back(val);
        }

        if (models.empty()) continue;

        BlockStateVariant bsv;
        for (const auto& m : models) {
            std::string mname = m.value("model", "");
            if (mname.empty()) continue;
            size_t mcPos = mname.find(':');
            if (mcPos != std::string::npos) mname = mname.substr(mcPos + 1);
            if (mname.find("block/") == 0) mname = mname.substr(6);
            BlockStateVariant::ModelEntry me;
            me.modelName = mname;
            me.rotX = m.value("x", 0);
            me.rotY = m.value("y", 0);
            me.uvlock = m.value("uvlock", false);
            bsv.models.push_back(std::move(me));
        }

        if (bsv.models.empty()) continue;

        auto props = parseVariantKey(variantKey);

        if (props.empty()) {
            hasSimpleVariant = true;
            entries.insert(entries.begin(), {props, bsv});
        } else {
            for (const auto& [prop, value] : props) {
                propValueSet[prop].insert(value);
            }
            entries.push_back({props, bsv});
        }
    }

    if (entries.empty()) return;

    if (hasSimpleVariant && propValueSet.empty()) {
        std::vector<BlockStateVariant> variants(1);
        variants[0] = entries[0].bsv;
        blockstateVariantCache[blockName] = std::move(variants);
        return;
    }

    // 已知属性值顺序
    static const std::unordered_map<std::string, std::vector<std::string>> knownPropOrder = {
        {"axis",     {"x", "y", "z"}},
        {"facing",   {"down", "up", "north", "south", "west", "east"}},
        {"half",     {"top", "bottom"}},
        {"type",     {"top", "bottom", "double"}},
        {"shape",    {"straight", "inner_left", "inner_right", "outer_left", "outer_right"}},
        {"face",     {"floor", "wall", "ceiling"}},
        {"egde",     {"none", "up", "side"}},
        {"attach",   {"floor", "ceiling", "single_wall", "double_wall"}},
        {"part",     {"foot", "head"}},
        {"vertical_direction", {"down", "up"}},
        {"instrument", {"harp", "basedrum", "snare", "hat", "bass", "flute", "bell", "guitar", "chime", "xylophone", "iron_xylophone", "cow_bell", "didgeridoo", "bit", "banjo", "pling"}},
        {"stage",    {"0", "1", "2", "3"}},
    };

    std::map<std::string, std::vector<std::string>> propValueList;
    for (const auto& [prop, collectedValues] : propValueSet) {
        auto knownIt = knownPropOrder.find(prop);
        if (knownIt != knownPropOrder.end()) {
            std::vector<std::string> ordered;
            for (const auto& val : knownIt->second) {
                if (collectedValues.find(val) != collectedValues.end()) ordered.push_back(val);
            }
            for (const auto& val : collectedValues) {
                if (std::find(ordered.begin(), ordered.end(), val) == ordered.end()) ordered.push_back(val);
            }
            propValueList[prop] = std::move(ordered);
        } else {
            if (collectedValues.size() == 2 &&
                collectedValues.find("true") != collectedValues.end() &&
                collectedValues.find("false") != collectedValues.end()) {
                propValueList[prop] = {"true", "false"};
            } else {
                std::vector<std::string> sorted(collectedValues.begin(), collectedValues.end());
                std::sort(sorted.begin(), sorted.end());
                propValueList[prop] = std::move(sorted);
            }
        }
    }

    // 用协议（blocks.json）的属性值顺序覆盖 blockstate 的推导顺序
    // 确保 offset 编码与协议完全一致
    {
        const auto* blockInfo = BlockRegistry::getInstance().getBlockInfoByName(blockName);
        if (blockInfo && !blockInfo->stateProperties.empty()) {
            for (const auto& sp : blockInfo->stateProperties) {
                auto plIt = propValueList.find(sp.name);
                if (plIt != propValueList.end()) {
                    // 保留与 blockstate JSON 实际出现值的交集，但按协议顺序
                    std::vector<std::string> ordered;
                    for (const auto& val : sp.values) {
                        auto psIt = propValueSet.find(sp.name);
                        if (psIt != propValueSet.end() &&
                            psIt->second.find(val) != psIt->second.end()) {
                            ordered.push_back(val);
                        }
                    }
                    if (!ordered.empty()) {
                        propValueList[sp.name] = std::move(ordered);
                    }
                }
            }
        }
    }

    std::unordered_set<std::string> autoAddedProps;

    // 检测并补充缺失的布尔属性
    if (propValueList.find("waterlogged") == propValueList.end() && !hasSimpleVariant) {
        const auto& registry = BlockRegistry::getInstance();
        const auto* blockInfo = registry.getBlockInfoByName(blockName);
        if (blockInfo) {
            int expectedStates = 1;
            for (const auto& [prop, values] : propValueList) {
                expectedStates *= (int)values.size();
            }
            int actualStates = blockInfo->maxStateId - blockInfo->minStateId + 1;
            if (actualStates > expectedStates && actualStates % expectedStates == 0) {
                int factor = actualStates / expectedStates;
                if (factor == 2 || factor == 4) {
                    autoAddedProps.insert("waterlogged");
                    propValueList["waterlogged"] = {"false", "true"};
                    for (auto& entry : entries) {
                        entry.props.emplace_back("waterlogged", "false");
                    }
                    if (factor == 4) {
                        autoAddedProps.insert("powered");
                        propValueList["powered"] = {"false", "true"};
                        for (auto& entry : entries) {
                            entry.props.emplace_back("powered", "false");
                        }
                    }
                    for (auto& entry : entries) {
                        std::sort(entry.props.begin(), entry.props.end(),
                            [](const auto& a, const auto& b) { return a.first < b.first; });
                    }
                }
            }
        }
    }

    // 第二遍：计算 offset
    int maxOffset = -1;
    std::unordered_map<int, BlockStateVariant> offsetMap;

    for (const auto& entry : entries) {
        if (entry.props.empty()) {
            offsetMap[0] = entry.bsv;
            if (0 > maxOffset) maxOffset = 0;
            continue;
        }

        int offset = 0;
        int stride = 1;
        for (int i = (int)entry.props.size() - 1; i >= 0; i--) {
            const auto& [propName, propValue] = entry.props[i];
            const auto& values = propValueList[propName];
            int valueIndex = 0;
            for (size_t vi = 0; vi < values.size(); vi++) {
                if (values[vi] == propValue) { valueIndex = (int)vi; break; }
            }
            offset += valueIndex * stride;
            stride *= (int)values.size();
        }
        offsetMap[offset] = entry.bsv;
        if (offset > maxOffset) maxOffset = offset;
    }

    // 填充未出现的 offset 为前一个变体
    int totalStates = 1;
    for (const auto& [prop, values] : propValueList) {
        totalStates *= (int)values.size();
    }
    int cacheSize = (maxOffset + 1) > totalStates ? (maxOffset + 1) : totalStates;
    std::vector<BlockStateVariant> orderedVariants(cacheSize);
    BlockStateVariant lastValid;
    for (int i = 0; i <= maxOffset; i++) {
        auto it = offsetMap.find(i);
        if (it != offsetMap.end()) {
            orderedVariants[i] = it->second;
            lastValid = it->second;
        } else {
            orderedVariants[i] = lastValid;
        }
    }
    // 如果实际状态数超过 maxOffset（如 auto-added waterlogged/powered 覆盖不全），用 lastValid 填充
    for (int i = maxOffset + 1; i < totalStates; i++) {
        orderedVariants[i] = lastValid;
    }

    blockstateVariantCache[blockName] = std::move(orderedVariants);
}

void TextureAtlas::parseMultipart(const std::string& blockName, const json& j) {
    if (!j.contains("multipart") || !j["multipart"].is_array()) return;

    const auto& multipart = j["multipart"];
    if (multipart.empty()) return;

    // 收集所有可能出现的属性和值
    struct MultiPartEntry {
        json when;          // when 条件（可能为 null）
        BlockStateVariant bsv;
    };
    std::vector<MultiPartEntry> entries;
    std::map<std::string, std::unordered_set<std::string>> propValues;

    for (const auto& part : multipart) {
        if (!part.is_object()) continue;

        json when = part.contains("when") ? part["when"] : json();

        // 解析 apply
        if (!part.contains("apply")) continue;
        const json& apply = part["apply"];
        std::vector<json> models;
        if (apply.is_array()) {
            for (const auto& m : apply) { if (m.is_object()) models.push_back(m); }
        } else if (apply.is_object()) {
            models.push_back(apply);
        }
        if (models.empty()) continue;

        BlockStateVariant bsv;
        for (const auto& m : models) {
            std::string mname = m.value("model", "");
            if (mname.empty()) continue;
            size_t mcPos = mname.find(':');
            if (mcPos != std::string::npos) mname = mname.substr(mcPos + 1);
            if (mname.find("block/") == 0) mname = mname.substr(6);
            BlockStateVariant::ModelEntry me;
            me.modelName = mname;
            me.rotX = m.value("x", 0);
            me.rotY = m.value("y", 0);
            me.uvlock = m.value("uvlock", false);
            bsv.models.push_back(std::move(me));
        }
        if (bsv.models.empty()) continue;

        entries.push_back({when, bsv});

        // 收集 when 条件中的属性值
        if (!when.is_null()) {
            std::function<void(const json&, bool)> collectProps;
            collectProps = [&](const json& node, bool isOr) {
                if (node.is_object()) {
                    for (auto it = node.begin(); it != node.end(); ++it) {
                        if (it.key() == "OR" && it->is_array()) {
                            for (const auto& item : *it) collectProps(item, true);
                        } else if (it->is_string()) {
                            propValues[it.key()].insert(it->get<std::string>());
                        }
                    }
                }
            };
            collectProps(when, false);
        }
    }

    if (entries.empty()) return;
    if (propValues.empty()) {
        std::vector<BlockStateVariant> single(1);
        single[0] = entries[0].bsv;
        blockstateVariantCache[blockName] = std::move(single);
        return;
    }

    // 优先使用 BlockRegistry 的完整属性列表
    std::vector<std::string> propNames;
    std::vector<std::vector<std::string>> propValueList;

    const auto* blockInfo = BlockRegistry::getInstance().getBlockInfoByName(blockName);
    if (blockInfo && !blockInfo->stateProperties.empty()) {
        for (const auto& sp : blockInfo->stateProperties) {
            propNames.push_back(sp.name);
            propValueList.push_back(sp.values);
        }
    } else {
        for (auto& [name, vals] : propValues) {
            vals.insert("none");
        }
        for (auto& [name, vals] : propValues) {
            propNames.push_back(name);
            std::vector<std::string> sorted(vals.begin(), vals.end());
            if (sorted.size() == 2 && sorted[0] == "false" && sorted[1] == "true") {
            } else {
                std::sort(sorted.begin(), sorted.end());
            }
            propValueList.push_back(std::move(sorted));
        }
    }

    // 计算总组合数
    int totalCombos = 1;
    for (const auto& vals : propValueList) totalCombos *= (int)vals.size();

    std::vector<BlockStateVariant> result(totalCombos);

    // 对每个排列组合检查条件
    for (int combo = 0; combo < totalCombos; combo++) {
        // 构建当前组合的属性映射
        std::unordered_map<std::string, std::string> stateProps;
        int tmp = combo;
        for (int i = (int)propNames.size() - 1; i >= 0; i--) {
            int idx = tmp % (int)propValueList[i].size();
            tmp /= (int)propValueList[i].size();
            stateProps[propNames[i]] = propValueList[i][idx];
        }

        // 检查所有 when 条件，累加所有匹配条目的模型
        BlockStateVariant accumulated;
        for (const auto& entry : entries) {
            if (entry.when.is_null()) {
                for (const auto& m : entry.bsv.models) {
                    accumulated.models.push_back(m);
                }
                continue;
            }

            std::function<bool(const json&)> matchWhen;
            matchWhen = [&](const json& node) -> bool {
                if (node.is_object()) {
                    for (auto it = node.begin(); it != node.end(); ++it) {
                        if (it.key() == "OR" && it->is_array()) {
                            for (const auto& item : *it) {
                                if (matchWhen(item)) return true;
                            }
                            return false;
                        }
                        if (it->is_string()) {
                            auto propIt = stateProps.find(it.key());
                            if (propIt == stateProps.end()) return false;
                            std::string valStr = it->get<std::string>();
                            // 支持逗号 , 和管道符 | 分隔的多值匹配（如 "side|up"）
                            size_t comma = valStr.find(',');
                            size_t pipe = valStr.find('|');
                            if (comma != std::string::npos || pipe != std::string::npos) {
                                size_t start = 0;
                                bool matched = false;
                                while (start < valStr.size()) {
                                    size_t end = valStr.find_first_of(",|", start);
                                    std::string v = (end == std::string::npos) ? valStr.substr(start) : valStr.substr(start, end - start);
                                    if (propIt->second == v) { matched = true; break; }
                                    if (end == std::string::npos) break;
                                    start = end + 1;
                                }
                                if (!matched) return false;
                            } else if (propIt->second != valStr) {
                                return false;
                            }
                        }
                    }
                    return true;
                }
                return false;
            };

            if (entry.when.is_object() && matchWhen(entry.when)) {
                for (const auto& m : entry.bsv.models) {
                    accumulated.models.push_back(m);
                }
            }
        }
        result[combo] = accumulated;
    }

    blockstateVariantCache[blockName] = std::move(result);
}

// ============================================================
// BlockState 变体查询
// ============================================================
const BlockStateVariant* TextureAtlas::getBlockStateVariant(
    const std::string& blockName, int32_t blockState, int32_t minStateId) const {
    auto it = blockstateVariantCache.find(blockName);
    if (it == blockstateVariantCache.end()) return nullptr;
    const auto& variants = it->second;
    int offset = (int)(blockState - minStateId);
    if (offset < 0 || offset >= (int)variants.size()) return nullptr;
    return &variants[offset];
}

std::vector<CollisionBox> TextureAtlas::getBlockCollisionBoxes(
        const std::string& blockName, int32_t blockState, int32_t minStateId) const {
    std::vector<CollisionBox> boxes;

    auto rotatePoint = [](float& x, float& y, float& z, int rotX, int rotY) {
        float cx = x - 8.0f;
        float cy = y - 8.0f;
        float cz = z - 8.0f;
        // 先 X 旋转（与 generateFromModel 一致：顺时针，角度取负）
        if (rotX != 0) {
            float rad = -rotX * (3.14159265f / 180.0f);
            float cosA = cosf(rad), sinA = sinf(rad);
            float ny = cy * cosA - cz * sinA;
            float nz = cy * sinA + cz * cosA;
            cy = ny; cz = nz;
        }
        // 再 Y 旋转
        if (rotY != 0) {
            float rad = -rotY * (3.14159265f / 180.0f);
            float cosA = cosf(rad), sinA = sinf(rad);
            float nx = cx * cosA + cz * sinA;
            float nz = -cx * sinA + cz * cosA;
            cx = nx; cz = nz;
        }
        x = cx + 8.0f;
        y = cy + 8.0f;
        z = cz + 8.0f;
    };

    // 先查 blockstate 变体
    if (minStateId >= 0) {
        const auto* variant = getBlockStateVariant(blockName, blockState, minStateId);
        if (variant && !variant->models.empty()) {
            for (const auto& me : variant->models) {
                const auto* model = getBlockModel(me.modelName);
                if (!model) continue;
                int rotX = me.rotX;
                int rotY = me.rotY;
                for (const auto& elem : model->elements) {
                    // 8 个顶点
                    float corners[8][3];
                    int idx = 0;
                    for (int i = 0; i < 2; ++i) {
                        float x = (i == 0) ? elem.from[0] : elem.to[0];
                        for (int j = 0; j < 2; ++j) {
                            float y = (j == 0) ? elem.from[1] : elem.to[1];
                            for (int k = 0; k < 2; ++k) {
                                float z = (k == 0) ? elem.from[2] : elem.to[2];
                                corners[idx][0] = x;
                                corners[idx][1] = y;
                                corners[idx][2] = z;
                                idx++;
                            }
                        }
                    }
                    // 应用旋转
                    for (int v = 0; v < 8; ++v) {
                        rotatePoint(corners[v][0], corners[v][1], corners[v][2], rotX, rotY);
                    }
                    // 计算包围盒
                    float minX = corners[0][0], maxX = corners[0][0];
                    float minY = corners[0][1], maxY = corners[0][1];
                    float minZ = corners[0][2], maxZ = corners[0][2];
                    for (int v = 1; v < 8; ++v) {
                        if (corners[v][0] < minX) minX = corners[v][0];
                        if (corners[v][0] > maxX) maxX = corners[v][0];
                        if (corners[v][1] < minY) minY = corners[v][1];
                        if (corners[v][1] > maxY) maxY = corners[v][1];
                        if (corners[v][2] < minZ) minZ = corners[v][2];
                        if (corners[v][2] > maxZ) maxZ = corners[v][2];
                    }
                    CollisionBox box;
                    box.minX = minX / 16.0f;
                    box.minY = minY / 16.0f;
                    box.minZ = minZ / 16.0f;
                    box.maxX = maxX / 16.0f;
                    box.maxY = maxY / 16.0f;
                    box.maxZ = maxZ / 16.0f;
                    boxes.push_back(box);
                }
                // 如果有模型元素，直接返回（通常楼梯只匹配一个模型）
                if (!model->elements.empty()) return boxes;
            }
        }
    }

    // fallback: 使用默认模型（不旋转）
    const auto* model = getBlockModel(blockName);
    if (model) {
        for (const auto& elem : model->elements) {
            CollisionBox box;
            box.minX = elem.from[0] / 16.0f;
            box.minY = elem.from[1] / 16.0f;
            box.minZ = elem.from[2] / 16.0f;
            box.maxX = elem.to[0] / 16.0f;
            box.maxY = elem.to[1] / 16.0f;
            box.maxZ = elem.to[2] / 16.0f;
            boxes.push_back(box);
        }
    }
    return boxes;
}

const std::string* TextureAtlas::getItemModelParent(const std::string& itemName) const {
    auto it = itemModelCache.find(itemName);
    if (it != itemModelCache.end()) return &it->second;
    return nullptr;
}
