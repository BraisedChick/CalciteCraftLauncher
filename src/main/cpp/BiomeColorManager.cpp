#include "BiomeColorManager.h"
#include "TextureLoader.h"
#include <android/log.h>
#include <algorithm>

#define LOG_TAG "BiomeColorManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

BiomeColorManager& BiomeColorManager::getInstance() {
    static BiomeColorManager instance;
    return instance;
}

bool BiomeColorManager::initialize(AAssetManager* assetManager) {
    if (ready) return true;
    LOGI("Initializing BiomeColorManager...");

    if (!loadColormaps(assetManager)) {
        LOGE("Failed to load colormap PNGs, using fallback defaults");
    }

    ready = true;
    LOGI("BiomeColorManager initialized successfully");
    return true;
}

bool BiomeColorManager::loadColormaps(AAssetManager* assetManager) {
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

void BiomeColorManager::applyServerBiomeMapping(const std::map<int32_t, BiomeEntry>& serverBiomes) {
    LOGI("Applying server biome registry mapping with %zu entries", serverBiomes.size());

    int loadedCount = 0;
    for (const auto& [serverId, entry] : serverBiomes) {
        if (serverId < 0 || serverId >= BIOME_COUNT) {
            LOGW("Server biome ID %d out of range [0,%d), skipping", serverId, BIOME_COUNT);
            continue;
        }

        biomes[serverId] = entry;
        loadedCount++;
    }

    LOGI("Applied %d server biome mappings", loadedCount);
}

void BiomeColorManager::sampleColor(const std::vector<uint8_t>& colormap,
                                     float temperature, float downfall,
                                     uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (colormap.empty()) {
        // 没有 colormap，返回默认绿色
        r = 127; g = 191; b = 80;
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
        r = 127; g = 191; b = 80;
    }
}

void BiomeColorManager::getGrassColor(int32_t biomeId, uint8_t& r, uint8_t& g, uint8_t& b) const {
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
        // 因为 grass colormap 的布局与 foliage 不同，不调整会落在白色区域
        float adjustedDownfall = entry.temperature * entry.downfall;
        sampleColor(grassColormap, entry.temperature, adjustedDownfall, r, g, b);
    } else {
        // fallback
        r = 127; g = 191; b = 80;
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
        sampleColor(foliageColormap, entry.temperature, entry.downfall, r, g, b);
    } else {
        // fallback
        r = 127; g = 191; b = 80;
    }
}

void BiomeColorManager::getWaterColor(int32_t biomeId, uint8_t& r, uint8_t& g, uint8_t& b) const {
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
