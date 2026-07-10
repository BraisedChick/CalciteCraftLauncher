#pragma once
#include <cstdint>
#include <string>
#include <map>

// Minecraft 协议版本常量
namespace ProtocolVersion {
    // 主要版本对应的协议号
    constexpr int V1_8   = 47;
    constexpr int V1_9   = 107;
    constexpr int V1_10  = 210;
    constexpr int V1_11  = 315;
    constexpr int V1_12  = 340;
    constexpr int V1_13  = 404;
    constexpr int V1_14  = 498;
    constexpr int V1_15  = 578;
    constexpr int V1_16  = 735;
    constexpr int V1_16_2 = 751;
    constexpr int V1_17  = 755;
    constexpr int V1_17_1 = 756;
    constexpr int V1_18  = 757;
    constexpr int V1_18_2 = 758;
    constexpr int V1_19  = 759;
    constexpr int V1_19_1 = 760;
    constexpr int V1_19_3 = 761;
    constexpr int V1_19_4 = 762;
    constexpr int V1_20  = 763;
    constexpr int V1_20_2 = 764;
    constexpr int V1_20_5 = 766;
    constexpr int V1_21  = 767;
    constexpr int V1_21_2 = 768;
    constexpr int V1_21_4 = 769;
    constexpr int V1_21_5 = 770;
    constexpr int V1_21_8 = 772;
    constexpr int V1_21_11 = 774;
    
    // 未来版本预留
    constexpr int V1_22  = 800;  // 预估
    constexpr int V1_23  = 850;  // 预估
    constexpr int V1_24  = 900;  // 预估
    constexpr int V1_25  = 950;  // 预估
    constexpr int V1_26  = 1000; // 预估
}

// 区块数据格式版本
enum class ChunkDataFormat {
    Legacy,      // 1.8 - 1.12: 全局调色板，Section 高度 16
    Modern,      // 1.13 - 1.17: Section 调色板，Section 高度 16
    Extended,    // 1.18+: Section 调色板，Section 高度 16，世界高度扩展
    Latest       // 1.20.5+: Registry-based 系统
};

// 方块状态编码方式
enum class BlockStateEncoding {
    DirectID,    // 1.8 - 1.12: 直接 ID (data value)
    Palette,     // 1.13 - 1.17: 调色板索引
    BitArray,    // 1.18+: 位数组 + 调色板
    Registry     // 1.20.5+: Registry ID
};

// 维度配置
struct DimensionConfig {
    int minY;           // 最小 Y 坐标
    int maxY;           // 最大 Y 坐标
    int sectionHeight;  // Section 高度（通常是 16）
    
    DimensionConfig() : minY(0), maxY(256), sectionHeight(16) {}
    DimensionConfig(int min, int max, int height) 
        : minY(min), maxY(max), sectionHeight(height) {}
    
    int getSectionCount() const {
        return (maxY - minY) / sectionHeight;
    }
};

// 协议版本特性
struct ProtocolFeatures {
    int protocolVersion;              // 协议版本号
    ChunkDataFormat chunkFormat;      // 区块数据格式
    BlockStateEncoding blockEncoding; // 方块编码方式
    DimensionConfig dimension;        // 维度配置
    
    // 构造函数
    ProtocolFeatures() 
        : protocolVersion(0),
          chunkFormat(ChunkDataFormat::Legacy),
          blockEncoding(BlockStateEncoding::DirectID) {}
    
    ProtocolFeatures(int version, ChunkDataFormat format, 
                     BlockStateEncoding encoding, const DimensionConfig& dim)
        : protocolVersion(version),
          chunkFormat(format),
          blockEncoding(encoding),
          dimension(dim) {}
};

// 版本管理器
class VersionManager {
public:
    static VersionManager& getInstance();
    
    // 设置当前协议版本
    void setProtocolVersion(int version);
    
    // 获取当前协议版本
    int getProtocolVersion() const { return currentProtocolVersion; }
    
    // 获取版本特性
    const ProtocolFeatures& getFeatures() const { return currentFeatures; }
    
    // 检查版本是否大于等于指定版本
    bool isAtLeast(int version) const { return currentProtocolVersion >= version; }
    
    // 检查版本是否在范围内
    bool isInRange(int minVersion, int maxVersion) const {
        return currentProtocolVersion >= minVersion && currentProtocolVersion <= maxVersion;
    }
    
    // 获取版本名称（用于日志）
    std::string getVersionName() const;
    
    // 获取维度配置
    const DimensionConfig& getDimensionConfig() const { return currentFeatures.dimension; }
    
    // 更新维度配置（维度切换时调用）
    void setDimensionConfig(int minY, int maxY) {
        currentFeatures.dimension = DimensionConfig(minY, maxY, 16);
    }
    
    // 获取区块数据格式
    ChunkDataFormat getChunkFormat() const { return currentFeatures.chunkFormat; }
    
    // 获取方块编码方式
    BlockStateEncoding getBlockEncoding() const { return currentFeatures.blockEncoding; }

private:
    VersionManager();
    
    int currentProtocolVersion;
    ProtocolFeatures currentFeatures;
    
    // 初始化版本特性映射表
    void initializeVersionMap();
    
    // 根据协议版本查找特性
    ProtocolFeatures lookupFeatures(int version);
    
    std::map<int, ProtocolFeatures> versionMap;
};
