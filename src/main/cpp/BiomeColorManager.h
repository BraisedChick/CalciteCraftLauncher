#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <memory>

// 生物群系颜色管理器
// 完全动态的生物群系注册系统，不依赖硬编码的 BIOME_NAMES
// 从服务端 RegistryHolder 获取完整的生物群系信息
class BiomeColorManager {
public:
    static BiomeColorManager& getInstance();

    // 初始化：加载 colormap（只，不再加载硬编码的 biome 数据）
    bool initialize();

    bool isReady() const { return ready; }

    // 生物群系数据条目
    struct BiomeEntry {
        float temperature = 0.5f;
        float downfall = 0.5f;
        bool hasFixedGrassColor = false;
        uint8_t fixedGrassR = 255, fixedGrassG = 255, fixedGrassB = 255;
        bool hasFixedFoliageColor = false;
        uint8_t fixedFoliageR = 255, fixedFoliageG = 255, fixedFoliageB = 255;
        bool hasFixedWaterColor = false;
        uint8_t fixedWaterR = 255, fixedWaterG = 255, fixedWaterB = 255;
    };

    // 清理所有注册表数据（用于切换服务器/维度时重置）
    void clear();

    // 应用服务器端的 biome 注册表数据（从 LoginPacket RegistryHolder 解析）
    // 建立完整的动态注册表
    void applyServerBiomeMapping(const std::map<std::string, BiomeEntry>& serverBiomes);

    // 设置服务器 biome ID → 我们内部索引的映射
    void setServerIdMapping(const std::map<int32_t, int32_t>& mapping);

    // 从服务器 registry 中的 name 和 id 建立映射
    void setServerIdMapping(const std::map<int32_t, std::string>& serverIdToName);

    // 根据服务器 biome ID 获取颜色（主要接口）
    void getGrassColor(int32_t serverBiomeId, uint8_t& r, uint8_t& g, uint8_t& b) const;
    void getFoliageColor(int32_t serverBiomeId, uint8_t& r, uint8_t& g, uint8_t& b) const;
    void getWaterColor(int32_t serverBiomeId, uint8_t& r, uint8_t& g, uint8_t& b) const;

    // 获取生物群系信息（用于调试）
    const BiomeEntry* getBiomeEntry(int32_t serverBiomeId) const;
    const std::string& getBiomeName(int32_t serverBiomeId) const;

    // 获取注册表大小
    size_t getBiomeCount() const { return biomeRegistry.size(); }

private:
    BiomeColorManager() = default;
    ~BiomeColorManager() = default;

    bool loadColormaps();

    // 从 colormap 采样颜色
    void sampleColor(const std::vector<uint8_t>& colormap, float temperature, float downfall,
                     uint8_t& r, uint8_t& g, uint8_t& b) const;

    // 内部结构：完整的生物群系注册表项
    struct BiomeRegistryEntry {
        BiomeEntry data;
        std::string name;
        int32_t serverId = -1;
        int32_t internalId = -1;
    };

    // 解析服务器 biome 名称（去除 "minecraft:" 前缀）
    std::string parseBiomeName(const std::string& fullName) const;

    // 查找或创建 biome 注册表项
    BiomeRegistryEntry& getOrRegisterBiome(const std::string& name, int32_t serverId);

    bool ready = false;

    static constexpr int COLORMAP_SIZE = 256;

    // colormap RGBA 数据 (256x256)
    std::vector<uint8_t> grassColormap;
    std::vector<uint8_t> foliageColormap;

    // 动态生物群系注册表：name -> BiomeRegistryEntry
    std::unordered_map<std::string, std::unique_ptr<BiomeRegistryEntry>> biomeRegistry;

    // 服务器 biome ID -> 内部索引
    std::unordered_map<int32_t, int32_t> serverIdToInternalId;

    // 服务器 biome ID -> 生物群系名称
    std::unordered_map<int32_t, std::string> serverIdToName;

    // 内部索引 -> 服务器 biome ID（反向查找）
    std::unordered_map<int32_t, int32_t> internalIdToServerId;

    // 内部索引 -> 生物群系名称
    std::unordered_map<int32_t, std::string> internalIdToName;
};
