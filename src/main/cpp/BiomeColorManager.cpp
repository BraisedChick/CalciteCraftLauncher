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

    // 不再加载硬编码的 biome 数据，等待服务器数据
    ready = true;
    LOGI("BiomeColorManager initialized successfully (dynamic mode)");
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


void BiomeColorManager::clear() {
    biomeRegistry.clear();
    serverIdToInternalId.clear();
    serverIdToName.clear();
    internalIdToServerId.clear();
    LOGI("Biome registry cleared");
}

void BiomeColorManager::applyServerBiomeMapping(const std::map<std::string, BiomeEntry>& serverBiomes) {
    LOGI("Applying server biome registry mapping with %zu entries", serverBiomes.size());

    // 清理旧数据
    clear();

    int loadedCount = 0;
    for (const auto& [fullName, entry] : serverBiomes) {
        // 解析 biome 名称
        std::string biomeName = parseBiomeName(fullName);

        // 查找或创建注册表项
        BiomeRegistryEntry& registryEntry = getOrRegisterBiome(biomeName, -1); // serverId 将在之后设置

        // 设置数据
        registryEntry.data = entry;
        registryEntry.name = fullName;

        loadedCount++;
        LOGI("Registered biome: %s (temperature: %.2f, downfall: %.2f)",
             biomeName.c_str(), entry.temperature, entry.downfall);
    }

    LOGI("Registered %d server biome mappings", loadedCount);
}

void BiomeColorManager::setServerIdMapping(const std::map<int32_t, int32_t>& mapping) {
    // 清理旧的映射
    serverIdToInternalId.clear();
    serverIdToName.clear();

    for (const auto& [serverId, internalId] : mapping) {
        // 查找注册表项
        auto it = biomeRegistry.begin();
        for (; it != biomeRegistry.end(); ++it) {
            if (it->second->internalId == internalId) {
                // 设置映射
                serverIdToInternalId[serverId] = internalId;
                serverIdToName[serverId] = it->second->name;

                // 设置服务器 ID
                it->second->serverId = serverId;

                // 设置反向映射
                internalIdToServerId[internalId] = serverId;

                LOGI("Mapped server ID %d to internal ID %d (%s)",
                     serverId, internalId, it->second->name.c_str());
                break;
            }
        }

        if (it == biomeRegistry.end()) {
            LOGW("Could not find internal ID %d in registry for server ID %d",
                 internalId, serverId);
        }
    }

    LOGI("Server biome ID mapping set (direct) with %zu entries", serverIdToInternalId.size());
}

void BiomeColorManager::setServerIdMapping(const std::map<int32_t, std::string>& serverIdToNameParam) {
    // 清理旧的映射
    serverIdToInternalId.clear();
    serverIdToName.clear();

    for (const auto& [serverId, fullName] : serverIdToNameParam) {
        // 解析 biome 名称
        std::string biomeName = parseBiomeName(fullName);

        // 查找注册表项
        auto it = biomeRegistry.find(biomeName);
        if (it != biomeRegistry.end()) {
            // 设置映射
            serverIdToInternalId[serverId] = it->second->internalId;
            serverIdToName[serverId] = fullName;

            // 设置服务器 ID
            it->second->serverId = serverId;

            // 设置反向映射
            internalIdToServerId[it->second->internalId] = serverId;

            LOGI("Mapped server ID %d to internal ID %d (%s)",
                 serverId, it->second->internalId, biomeName.c_str());
        } else {
            LOGW("Could not find biome registry entry for server ID %d (%s)",
                 serverId, fullName.c_str());
        }
    }

    LOGI("Server biome ID mapping set (by name) with %zu entries", serverIdToInternalId.size());
}

std::string BiomeColorManager::parseBiomeName(const std::string& fullName) const {
    size_t colonPos = fullName.find(':');
    if (colonPos != std::string::npos) {
        return fullName.substr(colonPos + 1);
    }
    return fullName;
}

BiomeColorManager::BiomeRegistryEntry& BiomeColorManager::getOrRegisterBiome(const std::string& name, int32_t serverId) {
    auto it = biomeRegistry.find(name);
    if (it != biomeRegistry.end()) {
        return *it->second;
    }

    // 创建新的注册表项
    auto entry = std::make_unique<BiomeRegistryEntry>();
    entry->name = name;
    entry->serverId = serverId;
    int32_t newInternalId = static_cast<int32_t>(biomeRegistry.size());
    entry->internalId = newInternalId;

    // 维护映射
    internalIdToName[newInternalId] = name;
    internalIdToServerId[newInternalId] = serverId;

    // 存储并返回引用
    BiomeRegistryEntry* entryPtr = entry.get();
    biomeRegistry[name] = std::move(entry);

    LOGI("Created new biome registry entry: %s (internal ID: %d)",
         name.c_str(), newInternalId);

    return *entryPtr;
}

const BiomeColorManager::BiomeEntry* BiomeColorManager::getBiomeEntry(int32_t serverBiomeId) const {
    auto it = serverIdToInternalId.find(serverBiomeId);
    if (it == serverIdToInternalId.end()) {
        return nullptr;
    }

    int32_t internalId = it->second;

    // 使用 internalIdToName 映射找到名称
    auto nameIt = internalIdToName.find(internalId);
    if (nameIt == internalIdToName.end()) {
        return nullptr;
    }

    // 然后在 biomeRegistry 中查找
    auto registryIt = biomeRegistry.find(nameIt->second);
    if (registryIt == biomeRegistry.end()) {
        return nullptr;
    }

    return &registryIt->second->data;
}

const std::string& BiomeColorManager::getBiomeName(int32_t serverBiomeId) const {
    static std::string empty = "";

    auto it = serverIdToName.find(serverBiomeId);
    if (it != serverIdToName.end()) {
        return it->second;
    }

    return empty;
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

void BiomeColorManager::getGrassColor(int32_t serverBiomeId, uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (!ready) {
        r = 255; g = 255; b = 255;
        return;
    }

    // 查找内部映射
    auto it = serverIdToInternalId.find(serverBiomeId);
    if (it == serverIdToInternalId.end()) {
        LOGW("Server biome ID %d not found in registry", serverBiomeId);
        r = 255; g = 255; b = 255;
        return;
    }

    int32_t internalId = it->second;
    auto nameIt = internalIdToName.find(internalId);
    if (nameIt == internalIdToName.end()) {
        r = 255; g = 255; b = 255;
        return;
    }
    auto registryIt = biomeRegistry.find(nameIt->second);
    if (registryIt == biomeRegistry.end()) {
        r = 255; g = 255; b = 255;
        return;
    }

    const auto& entry = registryIt->second->data;

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
        float clampedTemp = std::max(0.0f, std::min(1.0f, entry.temperature));
        float adjustedDownfall = clampedTemp * std::max(0.0f, std::min(1.0f, entry.downfall));
        sampleColor(grassColormap, clampedTemp, adjustedDownfall, r, g, b);
    } else {
        // fallback
        r = 170; g = 68; b = 170;
    }
}


void BiomeColorManager::getFoliageColor(int32_t serverBiomeId, uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (!ready) {
        r = 255; g = 255; b = 255;
        return;
    }

    // 查找内部映射
    auto it = serverIdToInternalId.find(serverBiomeId);
    if (it == serverIdToInternalId.end()) {
        LOGW("Server biome ID %d not found in registry", serverBiomeId);
        r = 255; g = 255; b = 255;
        return;
    }

    int32_t internalId = it->second;
    auto nameIt = internalIdToName.find(internalId);
    if (nameIt == internalIdToName.end()) {
        r = 255; g = 255; b = 255;
        return;
    }
    auto registryIt = biomeRegistry.find(nameIt->second);
    if (registryIt == biomeRegistry.end()) {
        r = 255; g = 255; b = 255;
        return;
    }

    const auto& entry = registryIt->second->data;

    // 如果有固定树叶颜色（如沼泽），使用固定值
    if (entry.hasFixedFoliageColor) {
        r = entry.fixedFoliageR;
        g = entry.fixedFoliageG;
        b = entry.fixedFoliageB;
        return;
    }

    // 从 colormap 采样
    if (!foliageColormap.empty()) {
        float clampedTemp = std::max(0.0f, std::min(1.0f, entry.temperature));
        float adjustedDownfall = clampedTemp * std::max(0.0f, std::min(1.0f, entry.downfall));
        sampleColor(foliageColormap, clampedTemp, adjustedDownfall, r, g, b);
    } else {
        // fallback
        r = 170; g = 68; b = 170;
    }
}

void BiomeColorManager::getWaterColor(int32_t serverBiomeId, uint8_t& r, uint8_t& g, uint8_t& b) const {
    if (!ready) {
        r = 0x3F; g = 0x76; b = 0xE4;
        return;
    }

    // 查找内部映射
    auto it = serverIdToInternalId.find(serverBiomeId);
    if (it == serverIdToInternalId.end()) {
        // 使用默认水色
        r = 0x3F; g = 0x76; b = 0xE4;
        return;
    }

    int32_t internalId = it->second;
    auto nameIt = internalIdToName.find(internalId);
    if (nameIt == internalIdToName.end()) {
        r = 0x3F; g = 0x76; b = 0xE4;
        return;
    }
    auto registryIt = biomeRegistry.find(nameIt->second);
    if (registryIt == biomeRegistry.end()) {
        r = 0x3F; g = 0x76; b = 0xE4;
        return;
    }

    const auto& entry = registryIt->second->data;

    if (entry.hasFixedWaterColor) {
        r = entry.fixedWaterR;
        g = entry.fixedWaterG;
        b = entry.fixedWaterB;
    } else {
        // 无指定水色时使用 Minecraft 默认水色
        r = 0x3F; g = 0x76; b = 0xE4;
    }
}
