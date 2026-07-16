#include "MinecraftVersion.h"
#include <android/log.h>

#define LOG_TAG "VersionManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// 单例实例
static VersionManager* g_instance = nullptr;

VersionManager& VersionManager::getInstance() {
    if (!g_instance) {
        g_instance = new VersionManager();
    }
    return *g_instance;
}

VersionManager::VersionManager() 
    : currentProtocolVersion(0) {
    initializeVersionMap();
}

void VersionManager::initializeVersionMap() {
    // 1.8 - 1.12: Legacy 格式
    versionMap[ProtocolVersion::V1_8] = ProtocolFeatures(
        ProtocolVersion::V1_8,
        ChunkDataFormat::Legacy,
        BlockStateEncoding::DirectID,
        DimensionConfig(0, 256, 16)
    );
    
    // 1.9 - 1.12 使用相同的格式
    versionMap[ProtocolVersion::V1_9] = ProtocolFeatures(
        ProtocolVersion::V1_9,
        ChunkDataFormat::Legacy,
        BlockStateEncoding::DirectID,
        DimensionConfig(0, 256, 16)
    );
    
    versionMap[ProtocolVersion::V1_12] = ProtocolFeatures(
        ProtocolVersion::V1_12,
        ChunkDataFormat::Legacy,
        BlockStateEncoding::DirectID,
        DimensionConfig(0, 256, 16)
    );
    
    // 1.13 - 1.17: Modern 格式（引入调色板）
    versionMap[ProtocolVersion::V1_13] = ProtocolFeatures(
        ProtocolVersion::V1_13,
        ChunkDataFormat::Modern,
        BlockStateEncoding::Palette,
        DimensionConfig(0, 256, 16)
    );
    
    versionMap[ProtocolVersion::V1_16] = ProtocolFeatures(
        ProtocolVersion::V1_16,
        ChunkDataFormat::Modern,
        BlockStateEncoding::Palette,
        DimensionConfig(0, 256, 16)
    );
    
    versionMap[ProtocolVersion::V1_17] = ProtocolFeatures(
        ProtocolVersion::V1_17,
        ChunkDataFormat::Modern,
        BlockStateEncoding::Palette,
        DimensionConfig(0, 256, 16)
    );
    
    // 1.18+: Extended 格式（世界高度扩展到 -64 ~ 320）
    versionMap[ProtocolVersion::V1_18] = ProtocolFeatures(
        ProtocolVersion::V1_18,
        ChunkDataFormat::Extended,
        BlockStateEncoding::BitArray,
        DimensionConfig(-64, 320, 16)
    );
    
    versionMap[ProtocolVersion::V1_18_2] = ProtocolFeatures(
        ProtocolVersion::V1_18_2,
        ChunkDataFormat::Extended,
        BlockStateEncoding::BitArray,
        DimensionConfig(-64, 320, 16)
    );
    
    versionMap[ProtocolVersion::V1_19] = ProtocolFeatures(
        ProtocolVersion::V1_19,
        ChunkDataFormat::Extended,
        BlockStateEncoding::BitArray,
        DimensionConfig(-64, 320, 16)
    );
    
    // 1.19.1-1.19.4: Extended 格式（与 1.18+ 相同）
    versionMap[ProtocolVersion::V1_19_4] = ProtocolFeatures(
        ProtocolVersion::V1_19_4,
        ChunkDataFormat::Extended,
        BlockStateEncoding::BitArray,
        DimensionConfig(-64, 320, 16)
    );
    
    versionMap[ProtocolVersion::V1_20] = ProtocolFeatures(
        ProtocolVersion::V1_20,
        ChunkDataFormat::Extended,
        BlockStateEncoding::BitArray,
        DimensionConfig(-64, 320, 16)
    );
    
    // 1.20.5+: Latest 格式（Registry-based）
    versionMap[ProtocolVersion::V1_20_5] = ProtocolFeatures(
        ProtocolVersion::V1_20_5,
        ChunkDataFormat::Latest,
        BlockStateEncoding::Registry,
        DimensionConfig(-64, 320, 16)
    );
    
    versionMap[ProtocolVersion::V1_21] = ProtocolFeatures(
        ProtocolVersion::V1_21,
        ChunkDataFormat::Latest,
        BlockStateEncoding::Registry,
        DimensionConfig(-64, 320, 16)
    );
    
    // 未来版本预留（使用最新的特性）
    versionMap[ProtocolVersion::V1_22] = ProtocolFeatures(
        ProtocolVersion::V1_22,
        ChunkDataFormat::Latest,
        BlockStateEncoding::Registry,
        DimensionConfig(-64, 384, 16)  // 假设未来会扩展高度
    );
    
    versionMap[ProtocolVersion::V1_26] = ProtocolFeatures(
        ProtocolVersion::V1_26,
        ChunkDataFormat::Latest,
        BlockStateEncoding::Registry,
        DimensionConfig(-128, 512, 16)  // 假设进一步扩展
    );
}

ProtocolFeatures VersionManager::lookupFeatures(int version) {
    // 精确匹配
    auto it = versionMap.find(version);
    if (it != versionMap.end()) {
        return it->second;
    }
    
    // 查找最接近的较低版本
    ProtocolFeatures fallback;
    int closestVersion = 0;
    
    for (const auto& pair : versionMap) {
        if (pair.first <= version && pair.first > closestVersion) {
            closestVersion = pair.first;
            fallback = pair.second;
        }
    }
    
    if (closestVersion > 0) {
        LOGI("Protocol version %d not found, using closest: %d", version, closestVersion);
        fallback.protocolVersion = version;
        return fallback;
    }
    
    // 完全找不到，返回默认值（1.8）
    LOGW("Protocol version %d not recognized, using default (1.8)", version);
    return ProtocolFeatures(
        version,
        ChunkDataFormat::Legacy,
        BlockStateEncoding::DirectID,
        DimensionConfig(0, 256, 16)
    );
}

void VersionManager::setProtocolVersion(int version) {
    currentProtocolVersion = version;
    currentFeatures = lookupFeatures(version);
    
    LOGI("Protocol version set to: %d (%s)", 
         version, getVersionName().c_str());
    LOGI("  Chunk format: %d", static_cast<int>(currentFeatures.chunkFormat));
    LOGI("  Block encoding: %d", static_cast<int>(currentFeatures.blockEncoding));
    LOGI("  Dimension: Y=%d to %d, sections=%d",
         currentFeatures.dimension.minY,
         currentFeatures.dimension.maxY,
         currentFeatures.dimension.getSectionCount());
}

std::string VersionManager::getVersionName() const {
    switch (currentProtocolVersion) {
        case ProtocolVersion::V1_8:   return "1.8";
        case ProtocolVersion::V1_9:   return "1.9";
        case ProtocolVersion::V1_10:  return "1.10";
        case ProtocolVersion::V1_11:  return "1.11";
        case ProtocolVersion::V1_12:  return "1.12";
        case ProtocolVersion::V1_13:  return "1.13";
        case ProtocolVersion::V1_14:  return "1.14";
        case ProtocolVersion::V1_15:  return "1.15";
        case ProtocolVersion::V1_16:  return "1.16";
        case ProtocolVersion::V1_16_2: return "1.16.2";
        case ProtocolVersion::V1_17:  return "1.17";
        case ProtocolVersion::V1_17_1: return "1.17.1";
        case ProtocolVersion::V1_18:  return "1.18";
        case ProtocolVersion::V1_18_2: return "1.18.2";
        case ProtocolVersion::V1_19:  return "1.19";
        case ProtocolVersion::V1_19_1: return "1.19.1";
        case ProtocolVersion::V1_19_3: return "1.19.3";
        case ProtocolVersion::V1_19_4: return "1.19.4";
        case ProtocolVersion::V1_20:  return "1.20";
        case ProtocolVersion::V1_20_2: return "1.20.2";
        case ProtocolVersion::V1_20_5: return "1.20.5";
        case ProtocolVersion::V1_21:  return "1.21";
        case ProtocolVersion::V1_21_2: return "1.21.2";
        case ProtocolVersion::V1_21_4: return "1.21.4";
        case ProtocolVersion::V1_21_5: return "1.21.5";
        case ProtocolVersion::V1_21_8: return "1.21.8";
        case ProtocolVersion::V1_21_11: return "1.21.11";
        default:
            if (currentProtocolVersion > ProtocolVersion::V1_21_11) {
                return "1.22+ (future)";
            }
            return "Unknown";
    }
}
