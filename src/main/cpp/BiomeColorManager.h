#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <map>

// 生物群系颜色管理器
// 加载 colormap/*.png 和 worldgen/biome/*.json，
// 根据 temperature 和 downfall 从 colormap 采样生成 biome 颜色
class BiomeColorManager {
public:
    static BiomeColorManager& getInstance();

    // 初始化：加载 colormap 和 biome 数据
    bool initialize();

    bool isReady() const { return ready; }

    // biome 数据条目
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

    // 应用服务器端的 biome 注册表数据（从 LoginPacket RegistryHolder 解析）
    // 用 name 匹配 BIOME_NAMES 找到本地索引，而非直接使用服务器 ID
    void applyServerBiomeMapping(const std::map<std::string, BiomeEntry>& serverBiomes);

    // 设置服务器 biome ID → 本地索引的映射（用于将 chunk 数据中的服务器 ID 转换为本地索引）
    void setServerIdMapping(const std::map<int32_t, int32_t>& mapping);
    // 从服务器 registry 中的 name 和 id 建立映射（需要 BIOME_NAMES 查找）
    void setServerIdMapping(const std::map<int32_t, std::string>& serverIdToName);

    // 根据 biome ID (0-63, 1.18.2 默认注册顺序) 获取草/树叶/水颜色
    void getGrassColor(int32_t biomeId, uint8_t& r, uint8_t& g, uint8_t& b) const;
    void getFoliageColor(int32_t biomeId, uint8_t& r, uint8_t& g, uint8_t& b) const;
    void getWaterColor(int32_t biomeId, uint8_t& r, uint8_t& g, uint8_t& b) const;

private:
    BiomeColorManager() = default;

    bool loadColormaps();
    bool loadBiomeDefaults();

    // 将服务器 biome ID 转换为本地索引（如果映射存在）
    int32_t resolveBiomeId(int32_t biomeId) const {
        auto it = serverIdToLocal.find(biomeId);
        return (it != serverIdToLocal.end()) ? it->second : biomeId;
    }

    // 从 colormap 采样颜色
    void sampleColor(const std::vector<uint8_t>& colormap, float temperature, float downfall,
                     uint8_t& r, uint8_t& g, uint8_t& b) const;

    bool ready = false;

    static constexpr int COLORMAP_SIZE = 256;
    static constexpr int BIOME_COUNT = 64;

    // colormap RGBA 数据 (256x256)
    std::vector<uint8_t> grassColormap;
    std::vector<uint8_t> foliageColormap;

    BiomeEntry biomes[BIOME_COUNT];

    // 服务器 biome ID → 本地 BIOME_NAMES 索引
    std::map<int32_t, int32_t> serverIdToLocal;
};
