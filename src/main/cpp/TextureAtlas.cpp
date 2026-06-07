#include "TextureAtlas.h"
#include "TextureLoader.h"
#include "BlockRegistry.h"
#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <utility>
#include <unordered_set>

#define LOG_TAG "TextureAtlas"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

TextureAtlas& TextureAtlas::getInstance() {
    static TextureAtlas instance;
    return instance;
}

// ============================================================
// JSON 解析辅助函数
// ============================================================

std::string TextureAtlas::jsonExtractString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": \"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    size_t end = json.find('\"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
}

std::string TextureAtlas::jsonExtractParent(const std::string& json) {
    return jsonExtractString(json, "parent");
}

std::unordered_map<std::string, std::string> TextureAtlas::jsonExtractTextures(const std::string& json) {
    std::unordered_map<std::string, std::string> result;

    size_t texPos = json.find("\"textures\"");
    if (texPos == std::string::npos) return result;

    size_t bracePos = json.find('{', texPos);
    if (bracePos == std::string::npos) return result;

    size_t braceEnd = json.find('}', bracePos);
    if (braceEnd == std::string::npos) return result;

    std::string section = json.substr(bracePos + 1, braceEnd - bracePos - 1);

    size_t pos = 0;
    while (pos < section.size()) {
        // 跳过空白和逗号
        while (pos < section.size() && (section[pos] == ' ' || section[pos] == '\t' ||
               section[pos] == '\n' || section[pos] == '\r' || section[pos] == ',')) pos++;
        if (pos >= section.size()) break;

        if (section[pos] != '"') break;
        size_t keyStart = pos + 1;
        size_t keyEnd = section.find('"', keyStart);
        if (keyEnd == std::string::npos) break;
        std::string key = section.substr(keyStart, keyEnd - keyStart);

        pos = section.find(':', keyEnd);
        if (pos == std::string::npos) break;
        pos++;

        while (pos < section.size() && (section[pos] == ' ' || section[pos] == '\t' ||
               section[pos] == '\n' || section[pos] == '\r')) pos++;
        if (pos >= section.size()) break;

        if (section[pos] == '"') {
            size_t valStart = pos + 1;
            size_t valEnd = section.find('"', valStart);
            if (valEnd == std::string::npos) break;
            std::string value = section.substr(valStart, valEnd - valStart);
            result[key] = value;
            pos = valEnd + 1;
        } else {
            break;
        }
    }

    return result;
}

// ============================================================
// 模型纹理解析（递归解析 parent 链）
// ============================================================

TextureAtlas::TextureMap TextureAtlas::resolveModelTextures(
    const std::string& modelName,
    const std::string& jsonContent,
    std::unordered_map<std::string, TextureMap>& cache,
    const std::unordered_map<std::string, std::string>* modelContentCache) {

    // 查缓存
    {
        auto it = cache.find(modelName);
        if (it != cache.end()) return it->second;
    }

    if (jsonContent.empty()) {
        cache[modelName] = {};
        return {};
    }

    auto ownTextures = jsonExtractTextures(jsonContent);
    std::string parent = jsonExtractParent(jsonContent);

    TextureMap result;

    if (!parent.empty()) {
        // 标准化 parent 路径：去掉 "minecraft:" 前缀
        std::string parentModel = parent;
        size_t colonPos = parentModel.find(':');
        if (colonPos != std::string::npos) {
            parentModel = parentModel.substr(colonPos + 1);
        }

        // 优先从内容缓存获取 parent 的 JSON，避免 ZIP I/O
        std::string parentJson;
        if (modelContentCache) {
            // 尝试从 block 模型缓存中找
            auto it = modelContentCache->find(parentModel);
            if (it != modelContentCache->end()) {
                parentJson = it->second;
            }
        }
        if (parentJson.empty()) {
            // 不在 block 缓存中，尝试从 item 目录读取
            parentJson = TextureLoader::readTextFromZip("models/" + parentModel + ".json");
        }

        // 递归解析 parent 链
        auto parentResolved = resolveModelTextures(parentModel, parentJson, cache, modelContentCache);
        result = std::move(parentResolved);

        // 合并自身纹理（child override parent）
        for (const auto& [key, value] : ownTextures) {
            if (value == "*") {
                // * 表示继承父级的同名变量，不做任何操作
                continue;
            } else if (!value.empty() && value[0] == '#') {
                // #varname 引用：从当前已合并的结果中查找
                std::string varName = value.substr(1);
                auto varIt = result.find(varName);
                if (varIt != result.end()) {
                    result[key] = varIt->second;
                } else {
                    // 暂时无法解析（目标变量由子模型定义），存储引用原样，后续不动点解析
                    result[key] = value;
                }
            } else {
                result[key] = value;
            }
        }

        // 不动点解析所有 #variable 引用
        // cube_column 定义 "down": "#end"，但 "end" 由子模型 oak_log 提供，
        // 经过此轮解析后 "down" → "block/oak_log_top"
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
    // 去命名空间前缀
    size_t colonPos = path.find(':');
    if (colonPos != std::string::npos) {
        path = path.substr(colonPos + 1);
    }
    // 去 "block/" 前缀
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
// 模型元素解析（ELEMENTS 解析）
// ============================================================

// 标准化纹理路径（去掉 "minecraft:" 前缀）
static std::string normalizeTexturePath(const std::string& path) {
    size_t colonPos = path.find(':');
    if (colonPos != std::string::npos) {
        return path.substr(colonPos + 1);
    }
    return path;
}

// 提取 JSON 数组中的 float 值 [x, y, z]
static bool extractFloatArray(const std::string& json, size_t startPos, float out[3]) {
    // 找到 [
    size_t bracketPos = json.find('[', startPos);
    if (bracketPos == std::string::npos) return false;
    size_t bracketEnd = json.find(']', bracketPos);
    if (bracketEnd == std::string::npos) return false;

    std::string content = json.substr(bracketPos + 1, bracketEnd - bracketPos - 1);
    int count = 0;
    size_t pos = 0;
    while (pos < content.size() && count < 3) {
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t' ||
               content[pos] == '\n' || content[pos] == '\r' || content[pos] == ',')) pos++;
        if (pos >= content.size()) break;
        char* end = nullptr;
        float val = std::strtof(content.c_str() + pos, &end);
        if (end == content.c_str() + pos) break;
        out[count++] = val;
        pos = (size_t)(end - content.c_str());
    }
    return count == 3;
}

// 提取 JSON 布尔值
static bool extractBool(const std::string& json, const std::string& key, bool defaultVal) {
    std::string search = "\"" + key + "\": ";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return defaultVal;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return defaultVal;
    if (json.substr(pos, 4) == "true") return true;
    if (json.substr(pos, 5) == "false") return false;
    return defaultVal;
}

// 提取 JSON 整数
static int extractIntValue(const std::string& json, const std::string& key, int defaultVal) {
    std::string search = "\"" + key + "\": ";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return defaultVal;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return defaultVal;
    char* end = nullptr;
    long val = std::strtol(json.c_str() + pos, &end, 10);
    if (end == json.c_str() + pos) return defaultVal;
    return (int)val;
}

// 提取 JSON float
static float extractFloatValue(const std::string& json, const std::string& key, float defaultVal) {
    std::string search = "\"" + key + "\": ";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return defaultVal;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return defaultVal;
    char* end = nullptr;
    float val = std::strtof(json.c_str() + pos, &end);
    if (end == json.c_str() + pos) return defaultVal;
    return val;
}

// 提取 JSON 字符串值
static std::string extractStringValue(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\": \"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    size_t end = json.find('\"', pos);
    if (end == std::string::npos) return "";
    return json.substr(pos, end - pos);
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

std::string TextureAtlas::jsonExtractElementsArray(const std::string& json) {
    size_t elemsPos = json.find("\"elements\"");
    if (elemsPos == std::string::npos) return "";

    size_t colonPos = json.find(':', elemsPos);
    if (colonPos == std::string::npos) return "";

    // 找到数组起始 [
    size_t arrayStart = json.find('[', colonPos);
    if (arrayStart == std::string::npos) return "";

    // 花括号嵌套匹配找到对应的 ]
    int depth = 0;
    bool inString = false;
    size_t pos = arrayStart;
    for (; pos < json.size(); pos++) {
        char c = json[pos];
        if (inString) {
            if (c == '\\') { pos++; continue; }
            if (c == '"') inString = false;
            continue;
        }
        if (c == '"') { inString = true; continue; }
        if (c == '{' || c == '[') depth++;
        if (c == '}' || c == ']') {
            depth--;
            if (depth == 0) break;
        }
    }
    if (depth != 0 || pos == json.size()) return "";

    return json.substr(arrayStart, pos - arrayStart + 1);
}

void TextureAtlas::parseElementRotation(ModelElementData& element, const std::string& rotationJson) {
    if (rotationJson.empty()) return;

    // 提取 origin
    extractFloatArray(rotationJson, 0, element.rotation.origin);

    // 提取 axis
    std::string axisStr = extractStringValue(rotationJson, "axis");
    if (axisStr == "x") element.rotation.axis = 0;
    else if (axisStr == "y") element.rotation.axis = 1;
    else if (axisStr == "z") element.rotation.axis = 2;

    // 提取 angle
    element.rotation.angle = extractFloatValue(rotationJson, "angle", 0.0f);

    // 提取 rescale
    element.rotation.rescale = extractBool(rotationJson, "rescale", false);
}

void TextureAtlas::parseElementFaces(ModelElementData& element,
                                     const std::string& facesJson,
                                     const TextureMap& resolvedTextures,
                                     const std::unordered_map<std::string, int>& pathToLayer) {
    // facesJson 是 { "north": {...}, "south": {...}, ... }
    // 遍历每个 face 对象找到键值对

    static const char* faceNames[] = {"down", "up", "north", "south", "west", "east"};
    for (const char* faceName : faceNames) {
        FaceDir dir = faceDirFromString(faceName);
        if (dir == FACE_NONE) continue;

        // 先找 "faceName"，再向后找第一个 {（忽略冒号和空白）
        std::string keySearch = "\"" + std::string(faceName) + "\"";
        size_t keyPos = facesJson.find(keySearch);
        if (keyPos == std::string::npos) continue;
        size_t braceStart = facesJson.find('{', keyPos + keySearch.size());
        if (braceStart == std::string::npos) continue;
        if (braceStart == std::string::npos) continue;

        int depth = 0;
        bool inStr = false;
        size_t braceEnd = braceStart;
        for (; braceEnd < facesJson.size(); braceEnd++) {
            char c = facesJson[braceEnd];
            if (inStr) { if (c == '\\') { braceEnd++; continue; } if (c == '"') inStr = false; continue; }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') depth++;
            if (c == '}') { depth--; if (depth == 0) break; }
        }
        if (depth != 0 || braceEnd == facesJson.size()) continue;

        std::string faceContent = facesJson.substr(braceStart + 1, braceEnd - braceStart - 1);

        ModelFaceData& face = element.faces[dir];
        element.hasFaces[dir] = true;
        face.shade = element.shade;

        // UV
        size_t uvPos = faceContent.find("\"uv\"");
        if (uvPos != std::string::npos) {
            extractFloatArray(faceContent, uvPos, face.uv);
        }

        // texture (#varname → 实际路径 → layer 索引)
        std::string texRef = extractStringValue(faceContent, "texture");
        if (!texRef.empty() && texRef[0] == '#') {
            std::string varName = texRef.substr(1);
            auto varIt = resolvedTextures.find(varName);
            if (varIt != resolvedTextures.end() && !varIt->second.empty()) {
                std::string normalized = normalizeTexturePath(varIt->second);
                auto layerIt = pathToLayer.find(normalized);
                if (layerIt != pathToLayer.end()) {
                    face.textureLayer = layerIt->second;
                } else {
                    // 尝试直接作为 minecraft:block/xxx 查找
                    // 可能路径以 "minecraft:" 开头但不在 map 中，
                    // fallback 到 0
                    face.textureLayer = 0;
                }
            }
        }

        // cullface
        std::string cullStr = extractStringValue(faceContent, "cullface");
        if (!cullStr.empty()) {
            face.cullface = (int8_t)faceDirFromString(cullStr);
        }

        // tintindex
        face.tintindex = (int8_t)extractIntValue(faceContent, "tintindex", -1);
    }
}

void TextureAtlas::parseSingleElement(ModelElementData& element,
                                      const std::string& elementJson,
                                      const TextureMap& resolvedTextures,
                                      const std::unordered_map<std::string, int>& pathToLayer) {
    // from
    extractFloatArray(elementJson, 0, element.from);
    // to
    size_t toPos = elementJson.find("\"to\"");
    if (toPos != std::string::npos) {
        extractFloatArray(elementJson, toPos, element.to);
    }

    // shade
    element.shade = extractBool(elementJson, "shade", true);

    // faces
    size_t facesPos = elementJson.find("\"faces\"");
    if (facesPos != std::string::npos) {
        size_t bracePos = elementJson.find('{', facesPos);
        if (bracePos != std::string::npos) {
            // 找到匹配的 }
            int depth = 0;
            bool inStr = false;
            size_t braceEnd = bracePos;
            for (; braceEnd < elementJson.size(); braceEnd++) {
                char c = elementJson[braceEnd];
                if (inStr) { if (c == '\\') { braceEnd++; continue; } if (c == '"') inStr = false; continue; }
                if (c == '"') { inStr = true; continue; }
                if (c == '{') depth++;
                if (c == '}') { depth--; if (depth == 0) break; }
            }
            if (depth == 0 && braceEnd != elementJson.size()) {
                std::string facesContent = elementJson.substr(bracePos, braceEnd - bracePos + 1);
                // 去掉了外层的 {}，使得 facesContent 是 {"north":{...}, ...} 格式
                // parseElementFaces 需要接收这个完整的内容再提取各个 face
                parseElementFaces(element, facesContent, resolvedTextures, pathToLayer);
            }
        }
    }

    // rotation
    size_t rotPos = elementJson.find("\"rotation\"");
    if (rotPos != std::string::npos) {
        size_t rotBrace = elementJson.find('{', rotPos);
        if (rotBrace != std::string::npos) {
            int depth = 0;
            bool inStr = false;
            size_t rotEnd = rotBrace;
            for (; rotEnd < elementJson.size(); rotEnd++) {
                char c = elementJson[rotEnd];
                if (inStr) { if (c == '\\') { rotEnd++; continue; } if (c == '"') inStr = false; continue; }
                if (c == '"') { inStr = true; continue; }
                if (c == '{') depth++;
                if (c == '}') { depth--; if (depth == 0) break; }
            }
            if (depth == 0 && rotEnd != elementJson.size()) {
                std::string rotContent = elementJson.substr(rotBrace, rotEnd - rotBrace + 1);
                parseElementRotation(element, rotContent);
            }
        }
    }
}

void TextureAtlas::jsonExtractElement(std::vector<ModelElementData>& elements,
                                      const std::string& json,
                                      const TextureMap& resolvedTextures,
                                      const std::unordered_map<std::string, int>& pathToLayer) {
    std::string arrayContent = jsonExtractElementsArray(json);
    if (arrayContent.empty()) return;

    // 解析数组中的每个 element 对象 { ... }
    size_t pos = 0;
    while (pos < arrayContent.size()) {
        // 找到下一个 {
        size_t objStart = arrayContent.find('{', pos);
        if (objStart == std::string::npos) break;

        // 匹配 }
        int depth = 0;
        bool inStr = false;
        size_t objEnd = objStart;
        for (; objEnd < arrayContent.size(); objEnd++) {
            char c = arrayContent[objEnd];
            if (inStr) { if (c == '\\') { objEnd++; continue; } if (c == '"') inStr = false; continue; }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') depth++;
            if (c == '}') { depth--; if (depth == 0) break; }
        }
        if (depth != 0 || objEnd == arrayContent.size()) break;

        std::string elementJson = arrayContent.substr(objStart, objEnd - objStart + 1);

        ModelElementData element;
        parseSingleElement(element, elementJson, resolvedTextures, pathToLayer);
        elements.push_back(std::move(element));

        pos = objEnd + 1;
    }
}

// 判断模型是否为简单完整立方体（1 element, from[0,0,0]-to[16,16,16], 无旋转, 6面全部cullface）
static bool checkIsSimpleCube(const std::vector<ModelElementData>& elements) {
    if (elements.size() != 1) return false;
    const auto& elem = elements[0];
    for (int i = 0; i < 3; i++) {
        if (elem.from[i] > 0.001f || elem.to[i] < 15.999f) return false;
    }
    if (elem.rotation.angle != 0.0f) return false;
    for (int i = 0; i < 6; i++) {
        if (!elem.hasFaces[i] || elem.faces[i].cullface < 0) return false;
    }
    return true;
}

bool TextureAtlas::resolveBlockModelElements(
    const std::string& blockName,
    const std::string& jsonContent,
    std::unordered_map<std::string, bool>& elementCache,
    const std::unordered_map<std::string, std::string>* modelContentCache) {

    // 查缓存
    {
        auto it = elementCache.find(blockName);
        if (it != elementCache.end()) return it->second;
        // 标记处理中（防止循环引用）
        elementCache[blockName] = false;
    }

    if (jsonContent.empty()) {
        elementCache[blockName] = false;
        return false;
    }

    // 检查自身是否有 elements
    std::string elementsArray = jsonExtractElementsArray(jsonContent);
    bool hasOwnElements = !elementsArray.empty();

    // 解析 parent
    std::string parent = jsonExtractParent(jsonContent);
    bool parentHasElements = false;

    if (!parent.empty()) {
        std::string parentModel = normalizeTexturePath(parent);

        std::string parentJson;
        if (modelContentCache) {
            auto it = modelContentCache->find(parentModel);
            if (it != modelContentCache->end()) {
                parentJson = it->second;
            }
        }
        if (parentJson.empty()) {
            parentJson = TextureLoader::readTextFromZip("models/" + parentModel + ".json");
        }

        parentHasElements = resolveBlockModelElements(parentModel, parentJson, elementCache, modelContentCache);
    }

    // 解析成功：只有自身有 elements 或继承父的
    if (!hasOwnElements && !parentHasElements) {
        elementCache[blockName] = false;
        return false;
    }

    // 现在解析 elements 数据
    // 首先获取该 block 的 resolved textures（复用已有的 resolutionCache）
    // 但我们已经解析过了，需要从 blockTextureMap 获取 resolved 纹理路径
    // 对于 elements 中的 #varname 引用，我们需要 resolvedTextures 映射

    // 重新解析这个 block 的 texture map
    std::unordered_map<std::string, TextureMap> tempCache;
    auto resolvedTextures = resolveModelTextures(blockName, jsonContent, tempCache, modelContentCache);

    // 创建或获取模型数据
    ResolvedBlockModel& model = blockModelCache[blockName];

    if (hasOwnElements) {
        // 自身定义 elements → 解析它
        jsonExtractElement(model.elements, jsonContent, resolvedTextures, texturePathToLayer);
    } else {
        // 继承父的 elements → 复制父的模型数据
        std::string parentModel = normalizeTexturePath(jsonExtractParent(jsonContent));
        auto parentIt = blockModelCache.find(parentModel);
        if (parentIt != blockModelCache.end()) {
            model = parentIt->second; // 复制
            // 但纹理引用需要在当前 block 的 resolvedTextures 下重新解析
            // 不过 elements 中的纹理引用已经在父级解析时用父级的 resolvedTextures 解析了
            // 如果子级定义了一些纹理变量，会导致父级 elements 中的 #varname 引用不同
            // 所以我们需要用当前 block 的 resolvedTextures 重新解析 faces 中的纹理索引

            // 简单但正确的方法：重新解析 elements（使用当前的 resolvedTextures）
            // 但保留几何数据（from/to/rotation）
            // 实际上，更健壮的做法是复制父的 elements 然后重新解析纹理 layer 索引

            // 获取父的原始 JSON 中的 elements
            std::string parentJson;
            if (modelContentCache) {
                auto it = modelContentCache->find(parentModel);
                if (it != modelContentCache->end()) parentJson = it->second;
            }
            if (parentJson.empty()) {
                parentJson = TextureLoader::readTextFromZip("models/" + parentModel + ".json");
            }

            if (!parentJson.empty()) {
                model.elements.clear();
                jsonExtractElement(model.elements, parentJson, resolvedTextures, texturePathToLayer);
            }
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

    if (progressCallback) progressCallback(0.0f, "读取模型文件...");

    // 1. 读取所有模型 JSON（一次 ZIP 遍历，避免后续逐文件读取）
    auto modelFiles = TextureLoader::readAllTextFromZip("models/block/");
    if (modelFiles.empty()) {
        LOGE("No model files found in ZIP at models/block/");
        // 最小回退：加载一些基础纹理
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

    // 2. 构建模型内容缓存（blockName → JSON 内容），避免后续逐文件 ZIP 读取
    std::unordered_map<std::string, std::string> modelContentCache;
    modelContentCache.reserve(modelFiles.size());
    for (const auto& [entryPath, content] : modelFiles) {
        std::string blockName = entryPath;
        size_t slashPos = blockName.rfind('/');
        if (slashPos != std::string::npos) blockName = blockName.substr(slashPos + 1);
        size_t extPos = blockName.rfind(".json");
        if (extPos != std::string::npos) blockName = blockName.substr(0, extPos);
        if (!blockName.empty()) {
            modelContentCache[blockName] = content;
        }
    }
    LOGI("Cached %zu model contents", modelContentCache.size());

    // 3. 解析模型缓存（父模型只需要解析一次）
    std::unordered_map<std::string, TextureMap> resolutionCache;

    // 4. 遍历所有 block 模型，解析纹理和模型 elements
    int parsedCount = 0;
    int totalModels = static_cast<int>(modelContentCache.size());
    std::unordered_map<std::string, bool> elementResolveCache;
    for (const auto& [blockName, content] : modelContentCache) {
        // 解析 parent 链（传入内容缓存避免重复 ZIP 读取）
        auto resolved = resolveModelTextures(blockName, content, resolutionCache, &modelContentCache);
        if (resolved.empty()) continue;

        // 提取 top/side/bottom 纹理
        ModelTextures mt;
        mt.top = getFirstTexture(resolved, {"up", "top", "all", "end", "particle"});
        mt.side = getFirstTexture(resolved, {"north", "south", "east", "west", "side", "all", "end", "particle"});
        mt.bottom = getFirstTexture(resolved, {"down", "bottom", "all", "end", "particle"});

        // 回退
        if (mt.top.empty())    mt.top = "block/stone";
        if (mt.side.empty())   mt.side = "block/stone";
        if (mt.bottom.empty()) mt.bottom = "block/stone";

        blockTextureMap[blockName] = mt;

        // 确保纹理路径已添加
        ensureTexture(mt.top);
        ensureTexture(mt.side);
        ensureTexture(mt.bottom);

        // 解析模型 elements（用于模型兼容渲染）
        // 策略：如果自身 JSON 有 elements 则用自身的，否则在 parent 链中查找
        if (!jsonExtractElementsArray(content).empty()) {
            // 自身有 elements，直接用当前 block 的 resolved textures 解析
            jsonExtractElement(blockModelCache[blockName].elements, content, resolved, texturePathToLayer);
        } else {
            // 自身无 elements，在 parent 链中查找
            std::string parent = jsonExtractParent(content);
            if (!parent.empty()) {
                std::string parentModel = normalizeTexturePath(parent);
                // 从 modelContentCache 或 ZIP 中获取父级 JSON
                std::string parentJson;
                auto cacheIt = modelContentCache.find(parentModel);
                if (cacheIt != modelContentCache.end()) {
                    parentJson = cacheIt->second;
                } else {
                    parentJson = TextureLoader::readTextFromZip("models/" + parentModel + ".json");
                }
                // 递归查找父链中的 elements（用当前 block 的 resolved textures）
                // 这里需要沿 parent 链向上找 elements，但每次都用当前 block 的纹理映射
                std::string currentJson = parentJson;
                std::string currentName = parentModel;
                while (!currentJson.empty()) {
                    if (!jsonExtractElementsArray(currentJson).empty()) {
                        jsonExtractElement(blockModelCache[blockName].elements, currentJson, resolved, texturePathToLayer);
                        break;
                    }
                    std::string nextParent = jsonExtractParent(currentJson);
                    if (nextParent.empty()) break;
                    std::string nextName = normalizeTexturePath(nextParent);
                    if (nextName == currentName) break; // 防止循环
                    // 获取父级的 JSON
                    auto nextIt = modelContentCache.find(nextName);
                    if (nextIt != modelContentCache.end()) {
                        currentJson = nextIt->second;
                    } else {
                        currentJson = TextureLoader::readTextFromZip("models/" + nextName + ".json");
                    }
                    currentName = nextName;
                }
            }
        }

        parsedCount++;
        if (progressCallback && (parsedCount % 200 == 0 || parsedCount == totalModels)) {
            float p = 0.01f + 0.04f * (float)parsedCount / (float)totalModels;
            char buf[64];
            snprintf(buf, sizeof(buf), "解析方块模型 %d/%d", parsedCount, totalModels);
            progressCallback(p, buf);
        }
    }

    // 循环结束后确保进度条走到最终位置（防止 parsedCount != totalModels 导致卡住）
    if (progressCallback) {
        float p = 0.05f;
        char buf[64];
        snprintf(buf, sizeof(buf), "解析方块模型 %d/%d", parsedCount, totalModels);
        progressCallback(p, buf);
    }

    LOGI("Parsed %d/%zu block models", parsedCount, modelFiles.size());

    // 统计带 elements 的 block model
    {
        int elemCount = 0;
        for (const auto& [name, model] : blockModelCache) {
            if (!model.elements.empty()) elemCount++;
        }
        LOGI("Resolved %d block models with geometry elements", elemCount);
    }

    // 预计算所有模型的 isSimpleCube 标志
    {
        int simpleCubeCount = 0;
        for (auto& [name, model] : blockModelCache) {
            model.isSimpleCube = checkIsSimpleCube(model.elements);
            if (model.isSimpleCube) simpleCubeCount++;
        }
        LOGI("Precomputed isSimpleCube: %d/%zu models are simple cubes",
             simpleCubeCount, blockModelCache.size());
    }

    // 4.5 加载 blockstate JSON，解析变体映射
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
                parseBlockState(blockName, content);
                bsCount++;
            }
        }
        LOGI("Parsed %d blockstate files", bsCount);
    }

    // 5. 强制加入特殊纹理（MeshGenerator 需要，但可能不被任何模型引用）
    ensureTexture("block/grass_block_side_overlay");
    ensureTexture("block/snow");

    // 6. 缓存特殊纹理索引（直接查 map，因为 mutex 已在 initialize 中锁定）
    auto findLayer = [this](const std::string& path) {
        auto it = texturePathToLayer.find(path);
        return it != texturePathToLayer.end() ? it->second : -1;
    };
    grassSideOverlayLayer = findLayer("block/grass_block_side_overlay");
    grassBlockSnowLayer = findLayer("block/grass_block_top");
    grassSideLayer = findLayer("block/grass_block_side");

    // 如果某些特殊纹理找不到，用 stone 回退
    if (grassSideOverlayLayer < 0) grassSideOverlayLayer = findLayer("block/stone");
    if (grassBlockSnowLayer < 0)   grassBlockSnowLayer = findLayer("block/stone");
    if (grassSideLayer < 0)        grassSideLayer = findLayer("block/stone");

    initialized = true;
    LOGI("TextureAtlas initialized: %d texture layers, %d block mappings",
         getLayerCount(), (int)blockTextureMap.size());

    // 打印前 20 个 block 映射 key 用于调试
    {
        int count = 0;
        for (const auto& [key, val] : blockTextureMap) {
            LOGI("  BlockMap[%s] = top:%s side:%s bottom:%s", key.c_str(),
                 val.top.c_str(), val.side.c_str(), val.bottom.c_str());
            if (++count >= 20) break;
        }
    }

    // 打印调试：前 20 个纹理
    for (int i = 0; i < std::min(20, getLayerCount()); i++) {
        LOGI("  Layer %d: %s", i, textureList[i].c_str());
    }

    return true;
}

// ============================================================
// 查询方块纹理
// ============================================================
BlockTextureConfig TextureAtlas::getBlockTexture(const std::string& blockName) const {
    if (!initialized) {
        return {0, 0, 0};
    }

    std::lock_guard<std::mutex> lock(mutex);

    auto it = blockTextureMap.find(blockName);
    if (it == blockTextureMap.end()) {
        // 调试：打印前 10 个未匹配的方块名
        static std::atomic<int> debugMissCount{0};
        int miss = debugMissCount.fetch_add(1);
        if (miss < 10) {
            LOGI("getBlockTexture: '%s' not in blockTextureMap (total misses: %d)", blockName.c_str(), miss);
        } else if (miss == 10) {
            LOGI("getBlockTexture: ... (suppressing further miss logs)");
        }

        // 未知方块：尝试 "block/name" 启发式查找（如 snow → block/snow）
        std::string heuristic = "block/" + blockName;
        auto heurIt = texturePathToLayer.find(heuristic);
        if (heurIt != texturePathToLayer.end()) {
            int layer = heurIt->second;
            return {layer, layer, layer};
        }

        // 回退到 stone 纹理
        auto stoneIt = texturePathToLayer.find("block/stone");
        int stoneLayer = (stoneIt != texturePathToLayer.end()) ? stoneIt->second : 0;
        if (stoneLayer < 0) stoneLayer = 0;
        return {stoneLayer, stoneLayer, stoneLayer};
    }

    // 标准化纹理路径：去掉 "minecraft:" 前缀（模型文件中常包含此前缀，
    // 但 ensureTexture() 存储时已去掉，所以查询时也需要去掉）
    auto normalizePath = [](const std::string& path) -> std::string {
        size_t colonPos = path.find(':');
        if (colonPos != std::string::npos) {
            return path.substr(colonPos + 1);
        }
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

// ============================================================
// 获取第 layer 层的纹理文件名（用于 GLRenderer 纹理数组加载）
// ============================================================
std::string TextureAtlas::getTextureFileName(int layer) const {
    if (layer < 0 || layer >= static_cast<int>(textureList.size())) {
        return "stone.png";
    }
    return textureList[layer];
}

// ============================================================
// 占位色（当纹理文件不存在时）
// ============================================================
void TextureAtlas::getPlaceholderColor(int /*layer*/, uint8_t& r, uint8_t& g, uint8_t& b) const {
    r = 0xAA; g = 0x44; b = 0xAA; // 紫色
}

// ============================================================
// 纹理路径 → 图层索引
// ============================================================
int TextureAtlas::getLayerByTexturePath(const std::string& texturePath) const {
    std::string normalized = texturePath;
    size_t colonPos = normalized.find(':');
    if (colonPos != std::string::npos) {
        normalized = normalized.substr(colonPos + 1);
    }

    std::lock_guard<std::mutex> lock(mutex);
    auto it = texturePathToLayer.find(normalized);
    if (it != texturePathToLayer.end()) return it->second;
    return -1;
}

// ============================================================
// 特殊纹理查询
// ============================================================
int TextureAtlas::getGrassSideOverlayLayer() const { return grassSideOverlayLayer; }
int TextureAtlas::getGrassBlockSnowLayer() const    { return grassBlockSnowLayer; }
int TextureAtlas::getGrassSideLayer() const         { return grassSideLayer; }

// ============================================================
// Blockstate 变体解析
// ============================================================

std::vector<std::pair<std::string, std::string>> TextureAtlas::parseVariantKey(const std::string& key) {
    // key 如 "axis=x" 或 "facing=east,half=bottom,shape=straight" 或 ""（空）
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
    // 按属性名字母序排序（Minecraft blockState 编码顺序）
    std::sort(result.begin(), result.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    return result;
}

void TextureAtlas::parseBlockState(const std::string& blockName, const std::string& json) {
    // 查找 "variants": {
    size_t varPos = json.find("\"variants\"");
    if (varPos == std::string::npos) {
        // 没有 variants → 尝试 multipart（玻璃板、栅栏等）
        parseMultipart(blockName, json);
        return;
    }

    size_t bracePos = json.find('{', varPos);
    if (bracePos == std::string::npos) return;

    // 大括号匹配提取 variants 对象内容
    int depth = 0;
    bool inStr = false;
    size_t braceEnd = bracePos;
    for (; braceEnd < json.size(); braceEnd++) {
        char c = json[braceEnd];
        if (inStr) { if (c == '\\') { braceEnd++; continue; } if (c == '"') inStr = false; continue; }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') depth++;
        if (c == '}') { depth--; if (depth == 0) break; }
    }
    if (depth != 0 || braceEnd == json.size()) return;

    std::string varContent = json.substr(bracePos + 1, braceEnd - bracePos - 1);

    // 第一遍：解析所有变体，收集各属性的所有可能值
    struct ParsedEntry {
        std::vector<std::pair<std::string, std::string>> props;
        BlockStateVariant bsv;
    };
    std::vector<ParsedEntry> entries;
    std::unordered_map<std::string, std::unordered_set<std::string>> propValueSet;
    bool hasSimpleVariant = false;

    size_t pos = 0;
    while (pos < varContent.size()) {
        size_t keyStart = varContent.find('"', pos);
        if (keyStart == std::string::npos) break;
        size_t keyEnd = varContent.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) break;

        std::string variantKey = varContent.substr(keyStart + 1, keyEnd - keyStart - 1);

        size_t colonPos = varContent.find(':', keyEnd);
        if (colonPos == std::string::npos) break;

        size_t valStart = colonPos + 1;
        while (valStart < varContent.size() &&
               (varContent[valStart] == ' ' || varContent[valStart] == '\t' ||
                varContent[valStart] == '\n' || varContent[valStart] == '\r'))
            valStart++;

        if (valStart >= varContent.size()) break;

        bool isArray = (varContent[valStart] == '[');
        size_t actualStart = varContent.find_first_of('{', valStart);
        if (actualStart == std::string::npos) { pos = valStart + 1; continue; }

        depth = 0;
        inStr = false;
        size_t valEnd = actualStart;
        char closeChar = isArray ? ']' : '}';
        for (; valEnd < varContent.size(); valEnd++) {
            char c = varContent[valEnd];
            if (inStr) { if (c == '\\') { valEnd++; continue; } if (c == '"') inStr = false; continue; }
            if (c == '"') { inStr = true; continue; }
            if (c == '{' || c == '[') depth++;
            if (c == closeChar) { depth--; if (depth == 0) break; }
        }
        if (depth != 0) { pos = valEnd + 1; continue; }

        size_t entryContentStart, entryContentEnd;
        if (isArray) {
            size_t firstObj = varContent.find('{', actualStart);
            if (firstObj == std::string::npos || firstObj > valEnd) { pos = valEnd + 1; continue; }
            int objDepth = 0;
            size_t objEnd = firstObj;
            inStr = false;
            for (; objEnd <= valEnd; objEnd++) {
                char c = varContent[objEnd];
                if (inStr) { if (c == '\\') { objEnd++; continue; } if (c == '"') inStr = false; continue; }
                if (c == '"') { inStr = true; continue; }
                if (c == '{') objDepth++;
                if (c == '}') { objDepth--; if (objDepth == 0) break; }
            }
            if (objDepth != 0) { pos = valEnd + 1; continue; }
            entryContentStart = firstObj;
            entryContentEnd = objEnd;
        } else {
            entryContentStart = actualStart;
            entryContentEnd = valEnd;
        }

        std::string entryContent = varContent.substr(entryContentStart, entryContentEnd - entryContentStart + 1);

        BlockStateVariant bsv;
        {
            std::string mname = extractStringValue(entryContent, "model");
            if (mname.empty()) { pos = valEnd + 1; continue; }
            size_t mcPos = mname.find(':');
            if (mcPos != std::string::npos) mname = mname.substr(mcPos + 1);
            if (mname.find("block/") == 0) mname = mname.substr(6);
            BlockStateVariant::ModelEntry me;
            me.modelName = mname;
            me.rotX = extractIntValue(entryContent, "x", 0);
            me.rotY = extractIntValue(entryContent, "y", 0);
            me.uvlock = extractBool(entryContent, "uvlock", false);
            bsv.models.push_back(std::move(me));
        }

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

        pos = valEnd + 1;
    }

    if (entries.empty()) return;

    // 无属性变体（如 grass 的 "" 键）
    if (hasSimpleVariant && propValueSet.empty()) {
        std::vector<BlockStateVariant> variants(1);
        variants[0] = entries[0].bsv;
        blockstateVariantCache[blockName] = std::move(variants);
        return;
    }

    // 已知属性值顺序（匹配 Minecraft BlockStateDefinition 定义顺序）
    static const std::unordered_map<std::string, std::vector<std::string>> knownPropOrder = {
        {"axis",     {"x", "y", "z"}},
        {"facing",   {"down", "up", "north", "south", "west", "east"}},
        {"half",     {"top", "bottom"}},
        {"type",     {"bottom", "top", "double"}},
        {"shape",    {"straight", "inner_left", "inner_right", "outer_left", "outer_right"}},
        {"face",     {"floor", "wall", "ceiling"}},
        {"egde",     {"none", "up", "side"}},
        {"attach",   {"floor", "ceiling", "single_wall", "double_wall"}},
        {"part",     {"foot", "head"}},
        {"vertical_direction", {"down", "up"}},
        {"instrument", {"harp", "basedrum", "snare", "hat", "bass", "flute", "bell", "guitar", "chime", "xylophone", "iron_xylophone", "cow_bell", "didgeridoo", "bit", "banjo", "pling"}},
        {"stage",    {"0", "1", "2", "3"}},
    };

    // 将属性值集合按已知顺序（或用字母序回退）排序
    std::map<std::string, std::vector<std::string>> propValueList;
    for (const auto& [prop, collectedValues] : propValueSet) {
        auto knownIt = knownPropOrder.find(prop);
        if (knownIt != knownPropOrder.end()) {
            // 用已知顺序，只保留 blockstate 中存在的值
            std::vector<std::string> ordered;
            for (const auto& val : knownIt->second) {
                if (collectedValues.find(val) != collectedValues.end()) {
                    ordered.push_back(val);
                }
            }
            // 已知列表中未覆盖的值（如 half=lower/upper）追到末尾
            for (const auto& val : collectedValues) {
                if (std::find(ordered.begin(), ordered.end(), val) == ordered.end()) {
                    ordered.push_back(val);
                }
            }
            propValueList[prop] = std::move(ordered);
        } else {
            // 检查是否是布尔属性（值恰好为 "true" 和 "false"）
            // Minecraft 的 BooleanProperty 使用 [true, false] 顺序，而非字母序
            if (collectedValues.size() == 2 &&
                collectedValues.find("true") != collectedValues.end() &&
                collectedValues.find("false") != collectedValues.end()) {
                propValueList[prop] = {"true", "false"};
            } else {
                // 其他未知属性：字母序回退
                std::vector<std::string> sorted(collectedValues.begin(), collectedValues.end());
                std::sort(sorted.begin(), sorted.end());
                propValueList[prop] = std::move(sorted);
            }
        }
    }

    std::unordered_set<std::string> autoAddedProps;

    // ===== 检测并补充缺失的布尔属性（waterlogged、powered 等）=====
    // Minecraft 1.13+ 中许多方块有 waterlogged/powered 等 boolean 属性，
    // 但 blockstate JSON 的 variant keys 中可能不包含它们（因为默认值为 false），
    // 导致偏移量计算缺少了 2^n 倍因子。
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
                // factor 必须是 2 的幂（缺失 n 个 boolean 属性时 factor = 2^n）
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
                    // 重新按属性名排序
                    for (auto& entry : entries) {
                        std::sort(entry.props.begin(), entry.props.end(),
                            [](const auto& a, const auto& b) { return a.first < b.first; });
                    }
                }
            }
        }
    }
    // ===== END 布尔属性检测 =====

    // 第二遍：用实际值列表计算 offset
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
                if (values[vi] == propValue) {
                    valueIndex = (int)vi;
                    break;
                }
            }
            offset += valueIndex * stride;
            stride *= (int)values.size();
        }
        offsetMap[offset] = entry.bsv;
        if (offset > maxOffset) maxOffset = offset;
    }

    // ===== 补充自动添加的布尔属性变体条目 =====
    // 对于自动检测添加的属性（如 powered、waterlogged），
    // 原始条目只包含默认值（false），需复制到所有 true 组合
    if (!autoAddedProps.empty()) {
        int expandBits = 0;
        for (const auto& prop : autoAddedProps) {
            auto setIt = propValueSet.find(prop);
            bool hasBothValues = (setIt != propValueSet.end() &&
                                  setIt->second.find("true") != setIt->second.end());
            if (!hasBothValues) expandBits++;
        }
        if (expandBits > 0) {
            int expandFactor = 1 << expandBits;
            std::unordered_map<int, BlockStateVariant> expandedMap;
            for (const auto& [off, variant] : offsetMap) {
                for (int b = 0; b < expandFactor; b++) {
                    expandedMap[off + b] = variant;
                }
            }
            offsetMap.swap(expandedMap);
            maxOffset = maxOffset + (expandFactor - 1);
        }
    }
    // ===== END 布尔属性补充 =====

    if (offsetMap.empty()) return;

    // 构建按 offset 索引的数组
    int arraySize = maxOffset + 1;
    if (hasSimpleVariant && arraySize < 1) arraySize = 1;
    std::vector<BlockStateVariant> variants(arraySize);
    for (auto& v : variants) {
        BlockStateVariant::ModelEntry me;
        me.modelName = blockName;
        v.models.push_back(std::move(me));
    }

    for (auto& [off, variant] : offsetMap) {
        if (off >= 0 && off < arraySize) {
            variants[off] = std::move(variant);
        }
    }

    blockstateVariantCache[blockName] = std::move(variants);
}

void TextureAtlas::parseMultipart(const std::string& blockName, const std::string& json) {
    // 查找 "multipart": [
    size_t arrStart = json.find("\"multipart\"");
    if (arrStart == std::string::npos) return;
    arrStart = json.find('[', arrStart);
    if (arrStart == std::string::npos) return;

    size_t arrEnd = arrStart;
    {
        int depth = 0;
        bool inStr = false;
        for (; arrEnd < json.size(); arrEnd++) {
            char c = json[arrEnd];
            if (inStr) { if (c == '\\') { arrEnd++; continue; } if (c == '"') inStr = false; continue; }
            if (c == '"') { inStr = true; continue; }
            if (c == '[') depth++;
            if (c == ']') { depth--; if (depth == 0) break; }
        }
        if (depth != 0 || arrEnd == json.size()) return;
    }

    // ===== 1. 解析所有 multipart entries =====
    struct MultipartWhen {
        std::vector<std::pair<std::string, std::string>> andProps; // AND 条件
    };
    struct MultipartEntry {
        std::vector<MultipartWhen> whenOrs;  // OR 条件（空=始终应用）
        BlockStateVariant::ModelEntry modelEntry;
    };
    std::vector<MultipartEntry> multipartEntries;

    size_t pos = arrStart + 1;
    while (pos < arrEnd) {
        size_t objStart = json.find('{', pos);
        if (objStart == std::string::npos || objStart >= arrEnd) break;

        int depth = 0;
        bool inStr = false;
        size_t objEnd = objStart;
        for (; objEnd < arrEnd; objEnd++) {
            char c = json[objEnd];
            if (inStr) { if (c == '\\') { objEnd++; continue; } if (c == '"') inStr = false; continue; }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') depth++;
            if (c == '}') { depth--; if (depth == 0) break; }
        }
        if (depth != 0 || objEnd >= arrEnd) break;

        std::string entryJson = json.substr(objStart, objEnd - objStart + 1);

        MultipartEntry entry;

        // 解析 "apply"（必需）
        size_t applyPos = entryJson.find("\"apply\"");
        if (applyPos == std::string::npos) { pos = objEnd + 1; continue; }
        size_t applyBrace = entryJson.find('{', applyPos);
        if (applyBrace == std::string::npos) { pos = objEnd + 1; continue; }
        depth = 0; inStr = false;
        size_t applyEnd = applyBrace;
        for (; applyEnd < entryJson.size(); applyEnd++) {
            char c = entryJson[applyEnd];
            if (inStr) { if (c == '\\') { applyEnd++; continue; } if (c == '"') inStr = false; continue; }
            if (c == '"') { inStr = true; continue; }
            if (c == '{') depth++;
            if (c == '}') { depth--; if (depth == 0) break; }
        }
        if (depth != 0) { pos = objEnd + 1; continue; }
        std::string applyContent = entryJson.substr(applyBrace, applyEnd - applyBrace + 1);

        std::string mname = extractStringValue(applyContent, "model");
        if (mname.empty()) { pos = objEnd + 1; continue; }
        size_t mcPos2 = mname.find(':');
        if (mcPos2 != std::string::npos) mname = mname.substr(mcPos2 + 1);
        if (mname.find("block/") == 0) mname = mname.substr(6);

        entry.modelEntry.modelName = mname;
        entry.modelEntry.rotX = extractIntValue(applyContent, "x", 0);
        entry.modelEntry.rotY = extractIntValue(applyContent, "y", 0);
        entry.modelEntry.uvlock = extractBool(applyContent, "uvlock", false);

        // 解析 "when"（可选）
        size_t whenPos = entryJson.find("\"when\"");
        if (whenPos != std::string::npos && whenPos < applyPos) {
            // when 可能是对象 {key:val,...} 或数组 [{...},{...}]
            size_t whenVal = entryJson.find_first_of("{[", whenPos + 6);
            if (whenVal != std::string::npos) {
                if (entryJson[whenVal] == '[') {
                    // OR 条件数组
                    size_t arrEnd2 = whenVal;
                    depth = 0; inStr = false;
                    for (; arrEnd2 < entryJson.size(); arrEnd2++) {
                        char c = entryJson[arrEnd2];
                        if (inStr) { if (c == '\\') { arrEnd2++; continue; } if (c == '"') inStr = false; continue; }
                        if (c == '"') { inStr = true; continue; }
                        if (c == '[') depth++;
                        if (c == ']') { depth--; if (depth == 0) break; }
                    }
                    if (depth == 0) {
                        // 解析数组中的每个对象
                        size_t ap = whenVal + 1;
                        while (ap < arrEnd2) {
                            size_t ob = entryJson.find('{', ap);
                            if (ob == std::string::npos || ob >= arrEnd2) break;
                            size_t oe = entryJson.find('}', ob);
                            if (oe == std::string::npos || oe >= arrEnd2) break;
                            std::string whenObj = entryJson.substr(ob, oe - ob + 1);
                            MultipartWhen mw;
                            // 提取所有 key:value 对
                            size_t kp = 0;
                            while ((kp = whenObj.find('"', kp)) != std::string::npos) {
                                size_t ke = whenObj.find('"', kp + 1);
                                if (ke == std::string::npos) break;
                                std::string key = whenObj.substr(kp + 1, ke - kp - 1);
                                size_t col2 = whenObj.find(':', ke);
                                if (col2 == std::string::npos) break;
                                size_t vs = col2 + 1;
                                while (vs < whenObj.size() && (whenObj[vs] == ' ' || whenObj[vs] == '\t')) vs++;
                                if (vs >= whenObj.size()) break;
                                std::string val;
                                if (whenObj[vs] == '"') {
                                    vs++;
                                    size_t ve = whenObj.find('"', vs);
                                    if (ve == std::string::npos) break;
                                    val = whenObj.substr(vs, ve - vs);
                                } else {
                                    size_t ve = whenObj.find_first_of(",}", vs);
                                    if (ve == std::string::npos) ve = whenObj.size();
                                    val = whenObj.substr(vs, ve - vs);
                                    while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
                                }
                                if (!key.empty() && !val.empty()) mw.andProps.emplace_back(key, val);
                                kp = (whenObj[vs == col2 + 1 ? vs : 0] == '"') ? whenObj.find('"', vs + val.size()) : whenObj.find_first_of(",}", vs);
                                if (kp == std::string::npos) break;
                            }
                            if (!mw.andProps.empty()) entry.whenOrs.push_back(std::move(mw));
                            ap = oe + 1;
                        }
                    }
                } else {
                    // 单个条件对象
                    size_t oe = entryJson.find('}', whenVal);
                    if (oe != std::string::npos) {
                        std::string whenObj = entryJson.substr(whenVal, oe - whenVal + 1);
                        MultipartWhen mw;
                        size_t kp = 0;
                        while ((kp = whenObj.find('"', kp)) != std::string::npos) {
                            size_t ke = whenObj.find('"', kp + 1);
                            if (ke == std::string::npos) break;
                            std::string key = whenObj.substr(kp + 1, ke - kp - 1);
                            size_t col2 = whenObj.find(':', ke);
                            if (col2 == std::string::npos) break;
                            size_t vs = col2 + 1;
                            while (vs < whenObj.size() && (whenObj[vs] == ' ' || whenObj[vs] == '\t')) vs++;
                            if (vs >= whenObj.size()) break;
                            std::string val;
                            if (whenObj[vs] == '"') {
                                vs++; size_t ve = whenObj.find('"', vs);
                                if (ve == std::string::npos) break;
                                val = whenObj.substr(vs, ve - vs);
                            } else {
                                size_t ve = whenObj.find_first_of(",}", vs);
                                if (ve == std::string::npos) ve = whenObj.size();
                                val = whenObj.substr(vs, ve - vs);
                                while (!val.empty() && (val.back() == ' ' || val.back() == '\t')) val.pop_back();
                            }
                            if (!key.empty()) mw.andProps.emplace_back(key, val);
                            kp = whenObj.find('"', ke + 1);
                            if (kp == std::string::npos) break;
                        }
                        if (!mw.andProps.empty()) entry.whenOrs.push_back(std::move(mw));
                    }
                }
            }
        }

        multipartEntries.push_back(std::move(entry));
        pos = objEnd + 1;
    }

    if (multipartEntries.empty()) return;

    // ===== 2. 从 when 条件中收集所有属性和值 =====
    std::map<std::string, std::unordered_set<std::string>> propValueSet;
    bool hasUnconditional = false;
    for (const auto& entry : multipartEntries) {
        if (entry.whenOrs.empty()) { hasUnconditional = true; continue; }
        for (const auto& when : entry.whenOrs) {
            for (const auto& [prop, val] : when.andProps) {
                propValueSet[prop].insert(val);
            }
        }
    }

    // ===== 3. 构建属性值列表 =====
    // 所有属性都视为布尔（multipart 方块的实际属性总是布尔）
    // 如果条件中只出现 "true"，补上 "false"
    std::map<std::string, std::vector<std::string>> propValueList;
    for (const auto& [prop, collected] : propValueSet) {
        if (collected.size() == 1 && collected.find("true") != collected.end()) {
            propValueList[prop] = {"true", "false"};
        } else if (collected.size() == 2 &&
                   collected.find("true") != collected.end() &&
                   collected.find("false") != collected.end()) {
            propValueList[prop] = {"true", "false"};
        } else {
            // 非布尔属性：字母序
            std::vector<std::string> sorted(collected.begin(), collected.end());
            std::sort(sorted.begin(), sorted.end());
            propValueList[prop] = std::move(sorted);
        }
    }

    // ===== 4. 计算总状态数，检测 waterlogged =====
    const auto& registry = BlockRegistry::getInstance();
    const auto* blockInfo = registry.getBlockInfoByName(blockName);
    int totalStates = 1;
    for (const auto& [prop, values] : propValueList) {
        totalStates *= (int)values.size();
    }

    bool hasAutoWaterlogged = false;
    bool hasAutoPowered = false;
    if (blockInfo && propValueList.find("waterlogged") == propValueList.end()) {
        int actualStates = blockInfo->maxStateId - blockInfo->minStateId + 1;
        if (actualStates > totalStates && actualStates % totalStates == 0) {
            int factor2 = actualStates / totalStates;
            if (factor2 == 2 || factor2 == 4) {
                hasAutoWaterlogged = true;
                propValueList["waterlogged"] = {"true", "false"};
                totalStates *= 2;
                if (factor2 == 4) {
                    hasAutoPowered = true;
                    propValueList["powered"] = {"true", "false"};
                    totalStates *= 2;
                }
            }
        }
    }

    if (totalStates <= 0) return;

    // ===== 5. 构建属性值索引映射（用于快速解码 offset）=====
    std::vector<std::string> propNames;
    std::vector<std::vector<std::string>> propValues;
    for (auto& [prop, values] : propValueList) {
        propNames.push_back(prop);
        propValues.push_back(values);
    }

    // ===== 6. 遍历所有 offset，匹配 multipart entries =====
    std::vector<BlockStateVariant> variants(totalStates);
    for (int off = 0; off < totalStates; off++) {
        // 解码 offset 对应的属性值
        std::unordered_map<std::string, std::string> propMap;
        int remaining = off;
        for (size_t i = 0; i < propNames.size(); i++) {
            int stride = 1;
            for (size_t j = i + 1; j < propNames.size(); j++) {
                stride *= (int)propValues[j].size();
            }
            int idx = remaining / stride;
            if (idx >= (int)propValues[i].size()) idx = 0;
            propMap[propNames[i]] = propValues[i][idx];
            remaining %= stride;
        }

        // 检查每个 multipart entry 是否匹配
        for (const auto& entry : multipartEntries) {
            bool matches = false;
            if (entry.whenOrs.empty()) {
                // 无条件：始终匹配，且只匹配一次
                variants[off].models.push_back(entry.modelEntry);
                continue;
            }
            // OR 语义：任一 when 块匹配即可
            for (const auto& when : entry.whenOrs) {
                bool andMatch = true;
                for (const auto& [prop, reqVal] : when.andProps) {
                    auto pmIt = propMap.find(prop);
                    if (pmIt == propMap.end() || pmIt->second != reqVal) {
                        andMatch = false;
                        break;
                    }
                }
                if (andMatch) {
                    matches = true;
                    break;
                }
            }
            if (matches) {
                variants[off].models.push_back(entry.modelEntry);
            }
        }
    }

    blockstateVariantCache[blockName] = std::move(variants);

    LOGI("parseMultipart: '%s' -> %d properties, %d states, %zu multipart entries",
         blockName.c_str(), (int)propNames.size(), (int)variants.size(), multipartEntries.size());
}

const BlockStateVariant* TextureAtlas::getBlockStateVariant(
    const std::string& blockName, int32_t blockState, int32_t minStateId) const {
    int32_t offset = blockState - minStateId;
    auto it = blockstateVariantCache.find(blockName);
    if (it == blockstateVariantCache.end()) return nullptr;
    if (offset < 0 || offset >= (int32_t)it->second.size()) return nullptr;
    return &it->second[offset];
}

// ============================================================
// 从模型元素解析碰撞箱
// ============================================================

std::vector<CollisionBox> TextureAtlas::getBlockCollisionBoxes(
    const std::string& blockName, int32_t blockState, int32_t minStateId) const
{
    // 1. 获取 blockstate 变体（模型名 + 旋转）
    const auto* variant = getBlockStateVariant(blockName, blockState, minStateId);
    const std::string* modelName = variant ? &variant->modelName() : &blockName;
    int bsRotX = variant ? variant->rotX() : 0;
    int bsRotY = variant ? variant->rotY() : 0;

    // 2. 获取模型 elements
    auto modelIt = blockModelCache.find(*modelName);
    if (modelIt == blockModelCache.end() || modelIt->second.elements.empty()) {
        return {};  // 无模型数据 → 调用方使用全方块碰撞
    }

    // 3. 将每个 element 转为碰撞箱（在 0-16 像素空间操作，最后 /16 归一化到 0-1）
    const auto& elements = modelIt->second.elements;
    std::vector<CollisionBox> boxes;
    boxes.reserve(elements.size());

    for (const auto& elem : elements) {
        // 获取 8 个顶点
        float corners[8][3];
        int idx = 0;
        for (int ci = 0; ci < 2; ci++) {
            float x = (ci == 0) ? elem.from[0] : elem.to[0];
            for (int cj = 0; cj < 2; cj++) {
                float y = (cj == 0) ? elem.from[1] : elem.to[1];
                for (int ck = 0; ck < 2; ck++) {
                    float z = (ck == 0) ? elem.from[2] : elem.to[2];
                    corners[idx][0] = x;
                    corners[idx][1] = y;
                    corners[idx][2] = z;
                    idx++;
                }
            }
        }

        // 应用 element 自身旋转（绕 origin，与渲染一致）
        if (elem.rotation.angle != 0.0f) {
            float ox = elem.rotation.origin[0];
            float oy = elem.rotation.origin[1];
            float oz = elem.rotation.origin[2];
            float rad = elem.rotation.angle * (3.14159265f / 180.0f);
            float cosA = cosf(rad), sinA = sinf(rad);

            for (int c = 0; c < 8; c++) {
                float dx = corners[c][0] - ox;
                float dy = corners[c][1] - oy;
                float dz = corners[c][2] - oz;
                float nx, ny, nz;

                switch (elem.rotation.axis) {
                    case 0: // X
                        ny = dy * cosA - dz * sinA;
                        nz = dy * sinA + dz * cosA;
                        nx = dx;
                        break;
                    case 1: // Y
                        nx = dx * cosA + dz * sinA;
                        nz = -dx * sinA + dz * cosA;
                        ny = dy;
                        break;
                    case 2: // Z
                        nx = dx * cosA - dy * sinA;
                        ny = dx * sinA + dy * cosA;
                        nz = dz;
                        break;
                    default:
                        nx = dx; ny = dy; nz = dz;
                }

                corners[c][0] = ox + nx;
                corners[c][1] = oy + ny;
                corners[c][2] = oz + nz;
            }
        }

        // 应用 blockstate 旋转（绕方块中心 8,8,8）
        if (bsRotX != 0 || bsRotY != 0) {
            for (int c = 0; c < 8; c++) {
                float px = corners[c][0] - 8.0f;
                float py = corners[c][1] - 8.0f;
                float pz = corners[c][2] - 8.0f;

                if (bsRotX != 0) {
                    float rad = -bsRotX * (3.14159265f / 180.0f);
                    float cosA = cosf(rad), sinA = sinf(rad);
                    float ny = py * cosA - pz * sinA;
                    float nz = py * sinA + pz * cosA;
                    py = ny; pz = nz;
                }
                if (bsRotY != 0) {
                    float rad = -bsRotY * (3.14159265f / 180.0f);
                    float cosA = cosf(rad), sinA = sinf(rad);
                    float nx = px * cosA + pz * sinA;
                    float nz = -px * sinA + pz * cosA;
                    px = nx; pz = nz;
                }

                corners[c][0] = px + 8.0f;
                corners[c][1] = py + 8.0f;
                corners[c][2] = pz + 8.0f;
            }
        }

        // 求旋转后的轴对齐边界并归一化到 0-1
        float minX = INFINITY, minY = INFINITY, minZ = INFINITY;
        float maxX = -INFINITY, maxY = -INFINITY, maxZ = -INFINITY;
        for (int c = 0; c < 8; c++) {
            float x = corners[c][0] / 16.0f;
            float y = corners[c][1] / 16.0f;
            float z = corners[c][2] / 16.0f;
            if (x < minX) minX = x;
            if (y < minY) minY = y;
            if (z < minZ) minZ = z;
            if (x > maxX) maxX = x;
            if (y > maxY) maxY = y;
            if (z > maxZ) maxZ = z;
        }

        // 跳过无体积或负体积元素
        if (maxX <= minX || maxY <= minY || maxZ <= minZ) continue;

        boxes.push_back({minX, minY, minZ, maxX, maxY, maxZ});
    }

    return boxes;
}
