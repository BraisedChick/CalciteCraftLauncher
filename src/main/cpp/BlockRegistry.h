#pragma once

#include <string>
#include <map>
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

    // 根据 blockState ID 获取方块信息
    const BlockInfo* getBlockInfo(int32_t blockState) const;

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

    // 解析 JSON 的辅助函数
    std::string extractString(const std::string& json, const std::string& key) const;
    int32_t extractInt(const std::string& json, const std::string& key) const;
};
