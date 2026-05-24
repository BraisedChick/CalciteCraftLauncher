#pragma once

#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <shared_mutex>

// 方块信息结构
struct BlockInfo {
    int32_t id;                    // 方块 registry ID
    std::string name;              // 方块名称（如 "stone"）
    std::string displayName;       // 显示名称（如 "Stone"）
    int32_t minStateId;            // 最小 blockState ID
    int32_t maxStateId;            // 最大 blockState ID
    int32_t defaultState;          // 默认 blockState ID
};

// 预计算的方块元数据（避免每次循环调用 getBlockName + 字符串比较）
struct BlockMetadata {
    std::string name;
    int texTop = 0;
    int texSide = 0;
    int texBottom = 0;
    float height = 1.0f;
    bool isPlant = false;
    bool isGrassBlock = false;
    bool isLeaves = false;
    bool isSnow = false;
    bool isFullBlock = true;
};

class BlockRegistry {
public:
    static BlockRegistry& getInstance() {
        static BlockRegistry instance;
        return instance;
    }

    // 从 JSON 文件加载方块数据
    bool loadFromJson(const std::string& jsonPath);

    // 根据 blockState ID 获取方块名称
    std::string getBlockName(int32_t blockState) const;

    // 根据 blockState ID 获取方块信息（返回指针，无拷贝）
    const BlockInfo* getBlockInfo(int32_t blockState) const;

    // 获取预计算的全部元数据（懒缓存，每个 blockState 仅计算一次）
    const BlockMetadata& getBlockMetadata(int32_t blockState) const;

    // 获取已加载的方块数量
    size_t getBlockCount() const { return blocks.size(); }

    // 是否已加载
    bool isLoaded() const { return loaded; }

private:
    BlockRegistry() : loaded(false) {}

    // blockState ID → blocks 数组索引的映射（避免指针失效问题）
    std::map<int32_t, size_t> stateToBlock;

    // 存储所有方块信息
    std::vector<BlockInfo> blocks;

    bool loaded;

    // 元数据缓存（mutable 允许在 const 方法中修改）
    mutable std::unordered_map<int32_t, BlockMetadata> metadataCache;
    mutable std::shared_mutex metadataMutex;  // 读写锁：多 worker 同时读，写独占

    // 计算单个 blockState 的元数据
    BlockMetadata computeMetadata(int32_t blockState) const;

    // 解析 JSON 的辅助函数
    std::string extractString(const std::string& json, const std::string& key) const;
    int32_t extractInt(const std::string& json, const std::string& key) const;
};
