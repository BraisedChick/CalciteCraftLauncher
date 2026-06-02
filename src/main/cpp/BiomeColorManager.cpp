#include "BiomeColorManager.h"
#include "TextureLoader.h"
#include <android/log.h>
#include <algorithm>
#include <unordered_map>

#define LOG_TAG "BiomeColorManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

BiomeColorManager& BiomeColorManager::getInstance() {
    static BiomeColorManager instance;
    return instance;
}

bool BiomeColorManager::initialize() {
    if (ready) return true;
    LOGI("Initializing BiomeColorManager...");

    if (!loadColormaps()) {
        LOGE("Failed to load colormap PNGs, using fallback defaults");
    }

    loadBiomeDefaults();

    ready = true;
    LOGI("BiomeColorManager initialized successfully");
    return true;
}

bool BiomeColorManager::loadColormaps() {
    // 加载 grass colormap
    {
        TextureData tex = TextureLoader::loadPNG("colormap/grass.png");
        if (!tex.data) {
            LOGE("Failed to load colormap/grass.png");
            return false;
        }
        if (tex.width != COLORMAP_SIZE || tex.height != COLORMAP_SIZE) {
            LOGW("Grass colormap size is %dx%d, expected %dx%d",
                 tex.width, tex.height, COLORMAP_SIZE, COLORMAP_SIZE);
        }
        size_t size = tex.width * tex.height * 4;
        grassColormap.resize(size);
        std::memcpy(grassColormap.data(), tex.data, size);
        LOGI("Loaded grass colormap: %dx%d", tex.width, tex.height);
    }

    // 加载 foliage colormap
    {
        TextureData tex = TextureLoader::loadPNG("colormap/foliage.png");
        if (!tex.data) {
            LOGE("Failed to load colormap/foliage.png");
            grassColormap.clear();
            return false;
        }
        if (tex.width != COLORMAP_SIZE || tex.height != COLORMAP_SIZE) {
            LOGW("Foliage colormap size is %dx%d, expected %dx%d",
                 tex.width, tex.height, COLORMAP_SIZE, COLORMAP_SIZE);
        }
        size_t size = tex.width * tex.height * 4;
        foliageColormap.resize(size);
        std::memcpy(foliageColormap.data(), tex.data, size);
        LOGI("Loaded foliage colormap: %dx%d", tex.width, tex.height);
    }

    return true;
}

// 1.18.2 默认 biome 注册顺序（name → ID），共 62 个
static const char* BIOME_NAMES[] = {
    "ocean", "plains", "desert", "windswept_hills", "forest",
    "taiga", "swamp", "river", "nether_wastes", "the_end",
    "frozen_ocean", "frozen_river", "snowy_plains", "snowy_beach",
    "windswept_gravelly_hills", "flower_forest", "birch_forest",
    "dark_forest", "old_growth_birch_forest", "old_growth_pine_taiga",
    "old_growth_spruce_taiga", "snowy_taiga", "savanna",
    "savanna_plateau", "badlands", "wooded_badlands", "eroded_badlands",
    "meadow", "grove", "snowy_slopes", "frozen_peaks", "jagged_peaks",
    "stony_peaks", "mushroom_fields", "dripstone_caves", "lush_caves",
    "deep_ocean", "deep_cold_ocean", "deep_frozen_ocean",
    "deep_lukewarm_ocean", "warm_ocean", "lukewarm_ocean", "cold_ocean",
    "sunflower_plains", "windswept_savanna", "bamboo_jungle", "jungle",
    "sparse_jungle", "beach", "stony_shore"
};
static const int BIOME_NAME_COUNT = sizeof(BIOME_NAMES) / sizeof(BIOME_NAMES[0]);

// 解析 JSON 中的数值（简单实现）
static float parseJsonFloat(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return -1.0f;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    size_t end = pos;
    while (end < json.size() && (json[end] == '.' || json[end] == '-' || (json[end] >= '0' && json[end] <= '9'))) end++;
    if (end == pos) return -1.0f;
    return std::stof(json.substr(pos, end - pos));
}

static int parseJsonInt(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return -1;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    char* end = nullptr;
    long val = strtol(json.c_str() + pos, &end, 10);
    if (end == json.c_str() + pos) return -1;
    return (int)val;
}

bool BiomeColorManager::loadBiomeDefaults() {
    LOGI("Loading biome defaults from ZIP...");

    // 一次遍历 ZIP，读取所有群系 JSON（减少 50+ 次独立 ZIP 查找）
    auto files = TextureLoader::readAllTextFromZip("worldgen/biome/");
    if (files.empty()) {
        LOGW("No biome files found in ZIP at worldgen/biome/");
        return false;
    }

    // 构建文件名 → 内容的映射表
    std::unordered_map<std::string, std::string> biomeFiles;
    for (auto& [path, content] : files) {
        // 提取 biome name: "worldgen/biome/plains.json" → "plains"
        size_t slashPos = path.rfind('/');
        size_t dotPos = path.rfind('.');
        if (slashPos == std::string::npos || dotPos == std::string::npos || dotPos <= slashPos) continue;
        std::string name = path.substr(slashPos + 1, dotPos - slashPos - 1);
        biomeFiles[std::move(name)] = std::move(content);
    }

    int loaded = 0;
    int maxId = BIOME_NAME_COUNT < BIOME_COUNT ? BIOME_NAME_COUNT : BIOME_COUNT;
    for (int id = 0; id < maxId; id++) {
        const char* name = BIOME_NAMES[id];
        auto it = biomeFiles.find(name);
        if (it == biomeFiles.end()) continue;

        const std::string& content = it->second;

        float temp = parseJsonFloat(content, "temperature");
        float downfall = parseJsonFloat(content, "downfall");

        if (temp != -1.0f) biomes[id].temperature = temp;
        if (downfall != -1.0f) biomes[id].downfall = downfall;

        // 解析 water_color（在 effects 对象内）
        std::string effectsKey = "\"effects\":";
        size_t effectsPos = content.find(effectsKey);
        if (effectsPos != std::string::npos) {
            size_t effectsEnd = content.find("}", effectsPos);
            if (effectsEnd != std::string::npos) {
                std::string effects = content.substr(effectsPos, effectsEnd - effectsPos + 1);
                int waterColor = parseJsonInt(effects, "water_color");
                if (waterColor >= 0) {
                    biomes[id].hasFixedWaterColor = true;
                    biomes[id].fixedWaterR = (uint8_t)((waterColor >> 16) & 0xFF);
                    biomes[id].fixedWaterG = (uint8_t)((waterColor >> 8) & 0xFF);
                    biomes[id].fixedWaterB = (uint8_t)(waterColor & 0xFF);
                }
                int grassColor = parseJsonInt(effects, "grass_color");
                if (grassColor >= 0) {
                    biomes[id].hasFixedGrassColor = true;
                    biomes[id].fixedGrassR = (uint8_t)((grassColor >> 16) & 0xFF);
                    biomes[id].fixedGrassG = (uint8_t)((grassColor >> 8) & 0xFF);
                    biomes[id].fixedGrassB = (uint8_t)(grassColor & 0xFF);
                }
                int foliageColor = parseJsonInt(effects, "foliage_color");
                if (foliageColor >= 0) {
                    biomes[id].hasFixedFoliageColor = true;
                    biomes[id].fixedFoliageR = (uint8_t)((foliageColor >> 16) & 0xFF);
                    biomes[id].fixedFoliageG = (uint8_t)((foliageColor >> 8) & 0xFF);
                    biomes[id].fixedFoliageB = (uint8_t)(foliageColor & 0xFF);
                }
            }
        }

        loaded++;
    }

    LOGI("Loaded %d biome defaults from ZIP", loaded);
    return loaded > 0;
}

void BiomeColorManager::applyServerBiomeMapping(const std::map<std::string, BiomeEntry>& serverBiomes) {
    LOGI("Applying server biome registry mapping with %zu entries", serverBiomes.size());

    int loadedCount = 0;
    for (const auto& [name, entry] : serverBiomes) {
        // 从 "minecraft:ocean" 格式提取名称
        std::string biomeName = name;
        size_t colonPos = biomeName.find(':');
        if (colonPos != std::string::npos) {
            biomeName = biomeName.substr(colonPos + 1);
        }

        // 在 BIOME_NAMES 中找到匹配的本地索引
        int localId = -1;
        for (int i = 0; i < BIOME_NAME_COUNT; i++) {
            if (biomeName == BIOME_NAMES[i]) {
                localId = i;
                break;
            }
        }

        if (localId < 0 || localId >= BIOME_COUNT) {
            LOGW("Server biome '%s' not found in local BIOME_NAMES, skipping", name.c_str());
            continue;
        }

        biomes[localId] = entry;
        loadedCount++;
    }

    LOGI("Applied %d server biome mappings", loadedCount);
}

void BiomeColorManager::setServerIdMapping(const std::map<int32_t, int32_t>& mapping) {
    serverIdToLocal = mapping;
    LOGI("Server biome ID mapping set (direct) with %zu entries", mapping.size());
}

void BiomeColorManager::setServerIdMapping(const std::map<int32_t, std::string>& serverIdToName) {
    serverIdToLocal.clear();
    for (const auto& [serverId, name] : serverIdToName) {
        std::string biomeName = name;
        size_t colonPos = biomeName.find(':');
        if (colonPos != std::string::npos) {
            biomeName = biomeName.substr(colonPos + 1);
        }
        for (int i = 0; i < BIOME_NAME_COUNT; i++) {
            if (biomeName == BIOME_NAMES[i]) {
                serverIdToLocal[serverId] = i;
                break;
            }
        }
    }
    LOGI("Server biome ID mapping set (by name) with %zu entries", serverIdToLocal.size());
}

void BiomeColorManager::sampleColor(const std::vector<uint8_t>& colormap,
                                     float temperature, float downfall,
                                     uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (colormap.empty()) {
        // 没有 colormap，返回默认绿色
        r = 170; g = 68; b = 170;
        return;
    }

    // Minecraft 原版算法：
    // temp 和 downfall 都 clamp 到 [0, 1]
    // x = (1 - temp) * 255
    // y = (1 - downfall) * 255
    float temp = std::max(0.0f, std::min(1.0f, temperature));
    float rain = std::max(0.0f, std::min(1.0f, downfall));

    int x = static_cast<int>((1.0f - temp) * (COLORMAP_SIZE - 1));
    int y = static_cast<int>((1.0f - rain) * (COLORMAP_SIZE - 1));

    x = std::max(0, std::min(COLORMAP_SIZE - 1, x));
    y = std::max(0, std::min(COLORMAP_SIZE - 1, y));

    int pixelIndex = (y * COLORMAP_SIZE + x) * 4;
    if (pixelIndex + 2 < static_cast<int>(colormap.size())) {
        r = colormap[pixelIndex + 0];
        g = colormap[pixelIndex + 1];
        b = colormap[pixelIndex + 2];
    } else {
        r = 170; g = 68; b = 170;
    }
}

void BiomeColorManager::getGrassColor(int32_t biomeId, uint8_t& r, uint8_t& g, uint8_t& b) const {
    biomeId = resolveBiomeId(biomeId);
    if (!ready || biomeId < 0 || biomeId >= BIOME_COUNT) {
        r = 255; g = 255; b = 255;
        return;
    }

    const auto& entry = biomes[biomeId];

    // 如果有固定颜色（如沼泽），使用固定值
    if (entry.hasFixedGrassColor) {
        r = entry.fixedGrassR;
        g = entry.fixedGrassG;
        b = entry.fixedGrassB;
        return;
    }

    // 从 colormap 采样
    if (!grassColormap.empty()) {
        // Minecraft 原版：grass 使用调整后的降雨量 (downfall * temperature)
        // 先用 clamped 后的温度乘降水，再算 y 坐标。否则负温度会导致 y 坐标落在底部
        float clampedTemp = std::max(0.0f, std::min(1.0f, entry.temperature));
        float adjustedDownfall = clampedTemp * std::max(0.0f, std::min(1.0f, entry.downfall));
        sampleColor(grassColormap, clampedTemp, adjustedDownfall, r, g, b);
    } else {
        // fallback
        r = 170; g = 68; b = 170;
    }

    // 调试日志：每 300 次调用输出一次 biomeId 映射
    static int grassColorCounter = 0;
    if (++grassColorCounter % 300 == 0) {
        float adjustedDownfall = entry.temperature * entry.downfall;
        LOGI("grassColor: biomeId=%d temp=%.2f rain=%.2f adjRain=%.2f color=(%d,%d,%d)",
             biomeId, entry.temperature, entry.downfall, adjustedDownfall, r, g, b);
    }
}

void BiomeColorManager::getFoliageColor(int32_t biomeId, uint8_t& r, uint8_t& g, uint8_t& b) const {
    biomeId = resolveBiomeId(biomeId);
    if (!ready || biomeId < 0 || biomeId >= BIOME_COUNT) {
        r = 255; g = 255; b = 255;
        return;
    }

    const auto& entry = biomes[biomeId];

    // 如果有固定树叶颜色（如沼泽），使用固定值
    if (entry.hasFixedFoliageColor) {
        r = entry.fixedFoliageR;
        g = entry.fixedFoliageG;
        b = entry.fixedFoliageB;
        return;
    }

    // 从 colormap 采样
    if (!foliageColormap.empty()) {
        // 与 grass 一样使用 adjustedDownfall 公式，
        // 这样寒冷群系会采样到 colormap 右下角的深色区域
        float clampedTemp = std::max(0.0f, std::min(1.0f, entry.temperature));
        float adjustedDownfall = clampedTemp * std::max(0.0f, std::min(1.0f, entry.downfall));
        sampleColor(foliageColormap, clampedTemp, adjustedDownfall, r, g, b);
    } else {
        // fallback
        r = 170; g = 68; b = 170;
    }
}

void BiomeColorManager::getWaterColor(int32_t biomeId, uint8_t& r, uint8_t& g, uint8_t& b) const {
    biomeId = resolveBiomeId(biomeId);
    if (!ready || biomeId < 0 || biomeId >= BIOME_COUNT) {
        r = 0x3F; g = 0x76; b = 0xE4;
        return;
    }

    const auto& entry = biomes[biomeId];

    if (entry.hasFixedWaterColor) {
        r = entry.fixedWaterR;
        g = entry.fixedWaterG;
        b = entry.fixedWaterB;
    } else {
        // 无指定水色时使用 Minecraft 默认水色
        r = 0x3F; g = 0x76; b = 0xE4;
    }
}
