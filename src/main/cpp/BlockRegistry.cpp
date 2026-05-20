#include "BlockRegistry.h"
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
