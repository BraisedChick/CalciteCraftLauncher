#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <mutex>
#include <functional>

// ============================================================
// 方块纹理配置：定义每个方块各面使用哪个纹理层
// ============================================================
struct BlockTextureConfig {
    int top = 0;
    int side = 0;
    int bottom = 0;
};

// ============================================================
// 方块模型元素系统 — 从 model JSON 的 "elements" 数组解析几何数据
// ============================================================

// 面方向
enum FaceDir : int8_t {
    FACE_DOWN = 0,
    FACE_UP = 1,
    FACE_NORTH = 2,
    FACE_SOUTH = 3,
    FACE_WEST = 4,
    FACE_EAST = 5,
    FACE_NONE = -1
};

// 一个面的解析后数据
struct ModelFaceData {
    int textureLayer = 0;    // 纹理数组 layer 索引（已在 initialize 时解析）
    float uv[4] = {0, 0, 16, 16}; // UV 坐标（Minecraft 0-16 像素空间）
    int8_t cullface = -1;    // 面剔除方向（FaceDir）,-1=不剔除
    int8_t tintindex = -1;   // -1=不染色, 0=草色, 1=树叶色
    bool shade = true;       // 漫反射光照
};

// 元素旋转
struct ElementRotation {
    float origin[3] = {8, 8, 8};
    int8_t axis = 1;         // 0=x, 1=y, 2=z
    float angle = 0.0f;      // -45, 45, 90, -90
    bool rescale = false;
};

// 一个元素（一个长方体，最多 6 个面）
struct ModelElementData {
    float from[3] = {0, 0, 0};
    float to[3] = {16, 16, 16};
    ModelFaceData faces[6];  // 按下标 FACE_DOWN..FACE_EAST
    bool hasFaces[6] = {false};
    ElementRotation rotation;
    bool shade = true;
};

// 解析后的完整方块模型
struct ResolvedBlockModel {
    std::vector<ModelElementData> elements;
    bool ambientocclusion = true;

};

// ============================================================
// Blockstate 变体：blockstate JSON 中单个变体的解析结果
// ============================================================
struct BlockStateVariant {
    struct ModelEntry {
        std::string modelName;
        int rotX = 0;
        int rotY = 0;
        bool uvlock = false;
    };
    std::vector<ModelEntry> models;  // 多个模型（multipart 方块如玻璃板）

    // 便捷访问：单模型变体时返回第一个模型
    const std::string& modelName() const {
        static const std::string s_empty;
        return models.empty() ? s_empty : models[0].modelName;
    }
    int rotX() const { return models.empty() ? 0 : models[0].rotX; }
    int rotY() const { return models.empty() ? 0 : models[0].rotY; }
};

// ============================================================
// 碰撞箱：从模型元素解析出的 AABB（block-local 0-1 坐标空间）
// ============================================================
struct CollisionBox {
    float minX, minY, minZ, maxX, maxY, maxZ;
};

// ============================================================
// TextureAtlas — 从 models/block/*.json 动态构建纹理映射和模型数据
//
// 工作流程：
//   1. initialize()：读取 resourcepack ZIP 中所有模型 JSON，解析 parent 链，
//      提取每个方块用到的纹理路径（如 "block/stone"），构建 路径→图层索引 映射
//      同时解析 elements 几何数据
//   2. getBlockTexture(name)：返回 {top, side, bottom} 图层索引
//   3. getBlockModel(name)：返回 ResolvedBlockModel 指针（含几何 elements 数据）
//   4. getTextureFileName(layer)：返回 "stone.png" 格式的文件名（用于 GLRenderer 加载）
// ============================================================
class TextureAtlas {
public:
    static TextureAtlas& getInstance();

    // 加载所有模型 JSON，解析 parent 链，构建纹理索引
    // progressCallback 可选，用于在长时间解析过程中显示进度
    bool initialize(std::function<void(float, const char*)> progressCallback = nullptr);

    // 获取方块各面的纹理图层索引
    BlockTextureConfig getBlockTexture(const std::string& blockName) const;

    // 获取第 layer 层的纹理文件名（如 "stone.png"），用于 GLRenderer 加载纹理数组
    std::string getTextureFileName(int layer) const;

    // 纹理层总数
    int getLayerCount() const { return static_cast<int>(textureList.size()); }

    // 获取第 layer 层的占位色（当 PNG 缺失时使用）
    void getPlaceholderColor(int layer, uint8_t& r, uint8_t& g, uint8_t& b) const;

    // 纹理路径 → 图层索引（用于 MeshGenerator 特殊纹理查询）
    int getLayerByTexturePath(const std::string& texturePath) const;

    // 特殊纹理图层（MeshGenerator 的 grass overlay 需要用）
    int getGrassSideOverlayLayer() const;
    int getGrassBlockSnowLayer() const;
    int getGrassSideLayer() const;

    // 获取破坏阶段纹理图层索引（0-9，返回-1表示未加载）
    int getDestroyStageLayer(int stage) const {
        if (stage < 0 || stage > 9) return -1;
        return destroyStageLayers[stage];
    }

    // 获取方块模型元素数据（模型兼容渲染用）
    // 返回 nullptr 表示无模型数据（应回退到旧立方体渲染）
    const ResolvedBlockModel* getBlockModel(const std::string& blockName) const;

    // 获取 blockstate 变体（根据 blockState ID 对应的朝向/属性选择模型和旋转）
    // blockState: 完整的 blockState ID, minStateId: 该方块的最小 blockState ID
    // 返回 nullptr 表示无双关变体（使用默认模型）
    const BlockStateVariant* getBlockStateVariant(const std::string& blockName,
                                                  int32_t blockState,
                                                  int32_t minStateId) const;

    // 获取方块碰撞箱列表（block-local 0-1 坐标空间，已应用 element 和 blockstate 旋转）
    // 返回空 vector 表示应使用全方块碰撞箱
    std::vector<CollisionBox> getBlockCollisionBoxes(const std::string& blockName,
                                                      int32_t blockState,
                                                      int32_t minStateId) const;

    // 获取物品对应的父级方块模型名（从 models/item/*.json 解析）
    // 返回 nullptr 表示该物品没有对应的方块模型
    const std::string* getItemModelParent(const std::string& itemName) const;

    // 获取物品方块模型映射表（用于预渲染物品栏图标）
    const std::unordered_map<std::string, std::string>& getItemModelCache() const {
        return itemModelCache;
    }

    bool isInitialized() const { return initialized; }

private:
    TextureAtlas() = default;

    // 单个方块的模型纹理（解析后的纹理路径，如 "block/stone"）
    struct ModelTextures {
        std::string top;
        std::string side;
        std::string bottom;
    };

    // blockName → 解析后的纹理路径
    std::unordered_map<std::string, ModelTextures> blockTextureMap;

    // 图层索引 → 短文件名（如 "stone.png"）
    std::vector<std::string> textureList;

    // 纹理路径 → 图层索引（"block/stone" → 3）
    std::unordered_map<std::string, int> texturePathToLayer;

    // item 名称 → 父级方块模型名（从 models/item/*.json 解析）
    std::unordered_map<std::string, std::string> itemModelCache;

    // 特殊纹理图层（缓存查询结果）
    int grassSideOverlayLayer = -1;
    int grassBlockSnowLayer = -1;
    int grassSideLayer = -1;

    // 破坏阶段纹理图层索引（destroy_stage_0 ~ destroy_stage_9）
    int destroyStageLayers[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

    // 确保特定纹理路径存在，返回图层索引
    int ensureTexture(const std::string& texturePath);

    // 从纹理路径生成短文件名（"block/stone" → "stone.png"）
    static std::string texturePathToFilename(const std::string& texturePath);

    // 递归解析模型 parent 链，返回完整纹理变量映射
    using TextureMap = std::unordered_map<std::string, std::string>;
    static TextureMap resolveModelTextures(
        const std::string& modelName,
        const std::string& jsonContent,
        std::unordered_map<std::string, TextureMap>& cache,
        const std::unordered_map<std::string, std::string>* modelContentCache = nullptr);

    // JSON 解析辅助
    static std::string jsonExtractString(const std::string& json, const std::string& key);
    static std::string jsonExtractParent(const std::string& json);
    static std::unordered_map<std::string, std::string> jsonExtractTextures(const std::string& json);

    // ===== 模型元素解析（模型兼容渲染） =====

    // 解析 JSON 中的 elements 数组
    static void jsonExtractElement(std::vector<ModelElementData>& elements,
                                   const std::string& json,
                                   const TextureMap& resolvedTextures,
                                   const std::unordered_map<std::string, int>& pathToLayer);

    // 提取 elements 字符串片段（整个 "elements": [...] 的内容）
    static std::string jsonExtractElementsArray(const std::string& json);

    // 提取面方向字符串（"north"/"south"/etc → FaceDir）
    static FaceDir faceDirFromString(const std::string& dir);

    // 解析单个 element 的 faces
    static void parseElementFaces(ModelElementData& element,
                                  const std::string& facesJson,
                                  const TextureMap& resolvedTextures,
                                  const std::unordered_map<std::string, int>& pathToLayer);

    // 解析 element rotation
    static void parseElementRotation(ModelElementData& element, const std::string& rotationJson);

    // 解析单个 element（from/to/faces/rotation）
    static void parseSingleElement(ModelElementData& element,
                                   const std::string& elementJson,
                                   const TextureMap& resolvedTextures,
                                   const std::unordered_map<std::string, int>& pathToLayer);

    // 沿 parent 链解析 block 的模型 elements（子无 elements 则继承父的）
    // 返回的 bool 表示是否有有效元素
    bool resolveBlockModelElements(
        const std::string& blockName,
        const std::string& jsonContent,
        std::unordered_map<std::string, bool>& elementCache,
        const std::unordered_map<std::string, std::string>* modelContentCache);

    // blockName → 解析后的模型数据
    std::unordered_map<std::string, ResolvedBlockModel> blockModelCache;

    // ===== Blockstate 解析 =====
    // blockName → 按 state offset 索引的变体数组
    std::unordered_map<std::string, std::vector<BlockStateVariant>> blockstateVariantCache;

    // 解析单个 blockstate JSON（variants 或 multipart）
    void parseBlockState(const std::string& blockName, const std::string& json);

    // 解析 multipart 格式 blockstate（玻璃板、栅栏等）
    void parseMultipart(const std::string& blockName, const std::string& json);

    // 从变体键解析属性值对（如 "facing=east,half=bottom" → [{facing,east},{half,bottom}]）
    static std::vector<std::pair<std::string, std::string>> parseVariantKey(const std::string& key);

    // 纹理路径 → 图层索引映射的引用（方便给解析函数传递）
    bool initialized = false;
    mutable std::mutex mutex;
};
