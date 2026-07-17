#pragma once

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <cstdint>

// 方块信息结构
struct BlockInfo {
    int32_t id;                    // 方块 registry ID
    std::string name;              // 方块名称（如 "stone"）
    std::string displayName;       // 显示名称（如 "Stone"）
    int32_t minStateId;            // 最小 blockState ID
    int32_t maxStateId;            // 最大 blockState ID
    int32_t defaultState;          // 默认 blockState ID
    
    // 属性定义顺序（从 blocks.json 的 states 数组提取）
    // 用于正确计算 state ID offset
    struct StateProperty {
        std::string name;          // 属性名（如 "facing"）
        std::string type;          // 属性类型（"bool", "enum", "int"）
        std::vector<std::string> values;  // 属性值列表（按协议顺序）
    };
    std::vector<StateProperty> stateProperties;

    float hardness = 0.0f;         // 方块硬度（-1 = 不可破坏，0 = 瞬间破坏）
    std::string material;          // 材质类型（"default", "mineable/pickaxe" 等）
    bool hasHarvestTools = false;  // 是否有 harvestTools（需正确工具才能掉落物品）
};

// 预计算的方块元数据（避免每次循环调用 getBlockName + 字符串比较）
struct BlockMetadata {
    std::string name;
    int texTop = 0;
    int texSide = 0;
    int texBottom = 0;
    float height = 1.0f;
    int32_t minStateId = 0;       // 该方块的最小的 blockState ID，用于 blockstate 解码
    bool isPlant = false;
    bool isGrassBlock = false;
    bool isLeaves = false;
    bool isSnow = false;
    bool isWater = false;
    bool isAir = false;
    bool isNoCollision = false;  // 火把、按钮等无碰撞方块
    bool isFullBlock = true;     // 几何上是否为完整 16x16x16 立方体（用于快速路径和面剔除）
    bool isOpaque = true;        // 是否不透明（false=玻璃等透明方块，相邻面不应被剔除）
    float hardness = 0.0f;       // 方块硬度（-1=不可破坏，0=瞬间破坏，>0=需要时间挖掘）
    bool requiresCorrectTool = false;  // 是否需正确工具才能掉落物品
    std::string material;          // 材质类型（"default", "mineable/pickaxe" 等）
};

class BlockRegistry {
public:
    static BlockRegistry& getInstance() {
        static BlockRegistry instance;
        return instance;
    }

    // 从 JSON 字符串加载方块数据
    bool loadFromJson(const std::string& jsonContent);

    // 从 JSON 字符串加载物品 ID→名称映射
    bool loadItems(const std::string& jsonContent);

    // 根据 blockState ID 获取方块名称
    std::string getBlockName(int32_t blockState) const;

    // 根据 blockState ID 获取方块信息（返回指针，无拷贝）
    const BlockInfo* getBlockInfo(int32_t blockState) const;

    // 获取预计算的全部元数据（懒缓存，每个 blockState 仅计算一次）
    const BlockMetadata& getBlockMetadata(int32_t blockState) const;

    // 预计算全部方块元数据（在 TextureAtlas 初始化后调用一次，之后 getBlockMetadata 无锁访问）
    void precomputeAll();

    // 获取已加载的方块数量
    size_t getBlockCount() const { return blocks.size(); }

    // 是否已加载
    bool isLoaded() const { return loaded; }

    // 根据 registry ID 获取物品名称（适用于物品栏中的 itemId）
    std::string getItemName(int32_t itemId) const;

    // 根据方块名称获取 BlockInfo
    const BlockInfo* getBlockInfoByName(const std::string& name) const;

private:
    BlockRegistry() : loaded(false) {}

    // blockState ID → blocks 数组索引的映射（避免指针失效问题）
    std::map<int32_t, size_t> stateToBlock;

    // 存储所有方块信息
    std::vector<BlockInfo> blocks;

    // registry ID → 名称映射（物品栏用）
    std::unordered_map<int32_t, std::string> idToName;

    bool loaded;

    // 元数据缓存（precomputeAll 一次性填充，之后只读）
    std::unordered_map<int32_t, BlockMetadata> metadataCache;

    // 计算单个 blockState 的元数据
    BlockMetadata computeMetadata(int32_t blockState) const;
};
