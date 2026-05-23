#pragma once

#include <cstdint>
#include <string>
#include <android/log.h>
#include "BlockRegistry.h"
#include "BiomeColorManager.h"

// ============================================================
// 纹理层索引分配
// 每个值对应纹理数组（GL_TEXTURE_2D_ARRAY）中的一个层
// ============================================================
enum TextureLayer : int {
    // 已有纹理（有 PNG 文件）
    TEX_GRASS_TOP = 0,      // grass_top.png
    TEX_GRASS_SIDE = 1,     // grass_side.png
    TEX_DIRT = 2,           // dirt.png

    // 新增纹理（需要对应 PNG 文件，否则使用占位色）
    TEX_STONE = 3,                // stone.png
    TEX_COBBLESTONE = 4,          // cobblestone.png
    TEX_OAK_PLANKS = 5,           // oak_planks.png
    TEX_OAK_LOG_TOP = 6,          // oak_log_top.png
    TEX_OAK_LOG_SIDE = 7,         // oak_log_side.png
    TEX_SPRUCE_PLANKS = 8,        // spruce_planks.png
    TEX_SPRUCE_LOG_TOP = 9,       // spruce_log_top.png
    TEX_SPRUCE_LOG_SIDE = 10,     // spruce_log_side.png
    TEX_SAND = 11,                // sand.png
    TEX_GRAVEL = 12,              // gravel.png
    TEX_WATER = 13,               // water.png
    TEX_OAK_LEAVES = 14,          // oak_leaves.png
    TEX_SPRUCE_LEAVES = 15,       // spruce_leaves.png
    TEX_GRASS_BLOCK_SNOW = 16,    // grass_block_snow.png
    TEX_SNOW = 17,                // snow.png
    TEX_ICE = 18,                 // ice.png
    TEX_GRASS_PLANT = 19,         // grass.png（草植物十字交叉纹理）
    TEX_GRASS_SIDE_OVERLAY = 20,  // grass_block_side_overlay.png（侧面覆盖层，被染色后叠加在 grass_side 上）

    TEXTURE_LAYER_COUNT,  // 纹理层总数
};

// ============================================================
// 方块纹理配置：定义每个方块各面使用哪个纹理层
// ============================================================
struct BlockTextureConfig {
    int top;     // 顶面纹理层索引
    int side;    // 侧面纹理层索引
    int bottom;  // 底面纹理层索引
};

// ============================================================
// 根据 blockState 查询纹理配置
// ============================================================
inline BlockTextureConfig getBlockTexture(int32_t blockState) {
    auto& registry = BlockRegistry::getInstance();
    std::string name = registry.getBlockName(blockState);

    // 草方块：顶面 grass_top，侧面 grass_side，底面 dirt
    if (name == "grass_block") {
        return {TEX_GRASS_TOP, TEX_GRASS_SIDE, TEX_DIRT};
    }

    // 泥土：全部 dirt
    if (name == "dirt" || name == "coarse_dirt"
        || name == "rooted_dirt" || name == "mud") {
        return {TEX_DIRT, TEX_DIRT, TEX_DIRT};
    }

    // 石头
    if (name == "stone" || name == "andesite"
        || name == "diorite" || name == "granite"
        || name == "deepslate" || name == "tuff"
        || name == "calcite" || name == "dripstone_block") {
        return {TEX_STONE, TEX_STONE, TEX_STONE};
    }

    // 圆石
    if (name == "cobblestone" || name == "mossy_cobblestone"
        || name == "stone_bricks" || name == "cracked_stone_bricks"
        || name == "mossy_stone_bricks") {
        return {TEX_COBBLESTONE, TEX_COBBLESTONE, TEX_COBBLESTONE};
    }

    // 橡木木板
    if (name == "oak_planks" || name == "oak_stairs"
        || name == "oak_slab" || name == "oak_fence") {
        return {TEX_OAK_PLANKS, TEX_OAK_PLANKS, TEX_OAK_PLANKS};
    }

    // 橡木原木：顶面原木顶部，侧面原木侧面
    if (name == "oak_log" || name == "oak_wood"
        || name == "stripped_oak_log" || name == "stripped_oak_wood") {
        return {TEX_OAK_LOG_TOP, TEX_OAK_LOG_SIDE, TEX_OAK_LOG_TOP};
    }

    // 云杉木板
    if (name == "spruce_planks" || name == "spruce_stairs"
        || name == "spruce_slab" || name == "spruce_fence") {
        return {TEX_SPRUCE_PLANKS, TEX_SPRUCE_PLANKS, TEX_SPRUCE_PLANKS};
    }

    // 云杉原木
    if (name == "spruce_log" || name == "spruce_wood"
        || name == "stripped_spruce_log" || name == "stripped_spruce_wood") {
        return {TEX_SPRUCE_LOG_TOP, TEX_SPRUCE_LOG_SIDE, TEX_SPRUCE_LOG_TOP};
    }

    // 沙子
    if (name == "sand" || name == "red_sand"
        || name == "sandstone" || name == "red_sandstone") {
        return {TEX_SAND, TEX_SAND, TEX_SAND};
    }

    // 砂砾
    if (name == "gravel") {
        return {TEX_GRAVEL, TEX_GRAVEL, TEX_GRAVEL};
    }

    // 橡树树叶
    if (name == "oak_leaves" || name == "birch_leaves"
        || name == "jungle_leaves" || name == "acacia_leaves"
        || name == "dark_oak_leaves" || name == "azalea_leaves"
        || name == "flowering_azalea_leaves") {
        return {TEX_OAK_LEAVES, TEX_OAK_LEAVES, TEX_OAK_LEAVES};
    }

    // 云杉树叶
    if (name == "spruce_leaves") {
        return {TEX_SPRUCE_LEAVES, TEX_SPRUCE_LEAVES, TEX_SPRUCE_LEAVES};
    }

    // 雪草方块
    if (name == "grass_block_snow") {
        return {TEX_GRASS_BLOCK_SNOW, TEX_GRASS_BLOCK_SNOW, TEX_DIRT};
    }

    // 雪片（不完整方块，可叠加 layers=1..8）
    if (name == "snow") {
        return {TEX_SNOW, TEX_SNOW, TEX_SNOW};
    }

    // 冰
    if (name == "ice" || name == "packed_ice" || name == "blue_ice" || name == "frosted_ice") {
        return {TEX_ICE, TEX_ICE, TEX_ICE};
    }

    // 植物（不完整方块）：草、蕨等使用专用 grass.png 纹理
    if (name == "grass" || name == "tall_grass"
        || name == "fern" || name == "large_fern") {
        return {TEX_GRASS_PLANT, TEX_GRASS_PLANT, TEX_GRASS_PLANT};
    }

    // 花
    if (name == "dandelion" || name == "poppy" || name == "blue_orchid"
        || name == "allium" || name == "azure_bluet" || name == "oxeye_daisy"
        || name == "cornflower" || name == "lily_of_the_valley"
        || name == "wither_rose" || name == "sunflower"
        || name == "lilac" || name == "rose_bush" || name == "peony") {
        return {TEX_OAK_PLANKS, TEX_OAK_PLANKS, TEX_OAK_PLANKS};
    }

    // 其他不完整方块：藤蔓、睡莲、枯灌木、甘蔗、蘑菇等
    if (name == "vine" || name == "lily_pad" || name == "dead_bush"
        || name == "sugar_cane" || name == "brown_mushroom"
        || name == "red_mushroom" || name == "cactus") {
        return {TEX_GRASS_TOP, TEX_GRASS_TOP, TEX_GRASS_TOP};
    }

    // 未知方块：根据 blockState ID 取模分配纹理，避免全部显示为石头
    int texIndex = TEX_STONE + (blockState % 10);
    if (texIndex >= TEXTURE_LAYER_COUNT) texIndex = TEX_STONE;
    return {texIndex, texIndex, texIndex};
}

// ============================================================
// 获取方块高度比例（1.0 = 完整方块，<1.0 = 不完整方块如雪片）
// ============================================================
inline float getBlockHeight(int32_t blockState) {
    if (blockState == 0) return 0.0f;

    auto& registry = BlockRegistry::getInstance();
    std::string name = registry.getBlockName(blockState);

    // 雪片：根据 blockState 在 "snow" 范围内的偏移量计算层数
    if (name == "snow") {
        auto* info = registry.getBlockInfo(blockState);
        if (info) {
            int stateCount = info->maxStateId - info->minStateId + 1;
            if (stateCount >= 8) {
                int layers = (blockState - info->minStateId) + 1;
                if (layers < 1) layers = 1;
                if (layers > 8) layers = 8;
                return layers / 8.0f;
            }
        }
        return 0.5f; // 无法确定层数，默认半格
    }

    return 1.0f; // 默认为完整方块
}

// 判断是否为植物类不完整方块（草、花、蕨等），需要十字交叉渲染
inline bool isPlant(int32_t blockState) {
    if (blockState == 0) return false;
    auto& registry = BlockRegistry::getInstance();
    std::string name = registry.getBlockName(blockState);
    return name == "grass" || name == "tall_grass"
        || name == "fern" || name == "large_fern"
        || name == "dead_bush" || name == "vine"
        || name == "lily_pad" || name == "sugar_cane"
        || name == "brown_mushroom" || name == "red_mushroom"
        || name == "dandelion" || name == "poppy" || name == "blue_orchid"
        || name == "allium" || name == "azure_bluet" || name == "oxeye_daisy"
        || name == "cornflower" || name == "lily_of_the_valley"
        || name == "wither_rose" || name == "sunflower"
        || name == "lilac" || name == "rose_bush" || name == "peony";
}

// 判断一个 blockState 是否为完整方块（不透明、遮挡相邻面）
inline bool isFullBlock(int32_t blockState) {
    if (blockState == 0) return false;
    if (isPlant(blockState)) return false; // 植物不遮挡相邻面

    auto& registry = BlockRegistry::getInstance();
    std::string name = registry.getBlockName(blockState);
    // 树叶不遮挡相邻面（使被树叶包围的木头等方块正常渲染）
    if (name.find("leaves") != std::string::npos) return false;

    return getBlockHeight(blockState) >= 1.0f;
}

// ============================================================
// 生物群系着色：根据 biome ID 返回草/树叶的 tint color
// 使用 BiomeColorManager 从 colormap PNG + biome JSON 采样
// ============================================================

// 获取 biome 的草染色（委托给 BiomeColorManager）
inline void getBiomeGrassColor(int32_t biomeId, uint8_t& r, uint8_t& g, uint8_t& b) {
    BiomeColorManager::getInstance().getGrassColor(biomeId, r, g, b);
}

// 获取 biome 的树叶染色
inline void getBiomeFoliageColor(int32_t biomeId, uint8_t& r, uint8_t& g, uint8_t& b) {
    BiomeColorManager::getInstance().getFoliageColor(biomeId, r, g, b);
}

// ============================================================
// 获取第 i 层的纹理文件名（用于加载）
// ============================================================
inline std::string getTextureFileName(int layer) {
    switch (layer) {
        case TEX_GRASS_TOP:      return "grass_block_top.png";
        case TEX_GRASS_SIDE:     return "grass_block_side.png";
        case TEX_DIRT:           return "dirt.png";
        case TEX_STONE:          return "stone.png";
        case TEX_COBBLESTONE:    return "cobblestone.png";
        case TEX_OAK_PLANKS:     return "oak_planks.png";
        case TEX_OAK_LOG_TOP:    return "oak_log_top.png";
        case TEX_OAK_LOG_SIDE:   return "oak_log.png";
        case TEX_SPRUCE_PLANKS:  return "spruce_planks.png";
        case TEX_SPRUCE_LOG_TOP: return "spruce_log_top.png";
        case TEX_SPRUCE_LOG_SIDE: return "spruce_log.png";
        case TEX_SAND:           return "sand.png";
        case TEX_GRAVEL:         return "gravel.png";
        case TEX_WATER:          return "water.png";
        case TEX_OAK_LEAVES:     return "oak_leaves.png";
        case TEX_SPRUCE_LEAVES:  return "spruce_leaves.png";
        case TEX_GRASS_BLOCK_SNOW: return "grass_block_snow.png";
        case TEX_SNOW:           return "snow.png";
        case TEX_ICE:            return "ice.png";
        case TEX_GRASS_PLANT:    return "grass.png";
        case TEX_GRASS_SIDE_OVERLAY: return "grass_block_side_overlay.png";
        default:                 return "stone.png";
    }
}

// ============================================================
// 获取第 i 层的占位纹理颜色（当 PNG 文件不存在时使用）
// ============================================================
inline void getPlaceholderColor(int layer, uint8_t& r, uint8_t& g, uint8_t& b) {
    switch (layer) {
        case TEX_GRASS_TOP:      r = 0x7C; g = 0xB3; b = 0x42; break; // 草绿
        case TEX_GRASS_SIDE:     r = 0x55; g = 0x8B; b = 0x2F; break; // 深绿
        case TEX_DIRT:           r = 0x79; g = 0x55; b = 0x48; break; // 棕色
        case TEX_STONE:          r = 0x9E; g = 0x9E; b = 0x9E; break; // 灰色
        case TEX_COBBLESTONE:    r = 0x75; g = 0x75; b = 0x75; break; // 深灰
        case TEX_OAK_PLANKS:     r = 0xBC; g = 0xAA; b = 0xA4; break; // 浅木色
        case TEX_OAK_LOG_TOP:    r = 0x8D; g = 0x6B; b = 0x4E; break; // 年轮色
        case TEX_OAK_LOG_SIDE:   r = 0x6D; g = 0x4F; b = 0x3A; break; // 树皮色
        case TEX_SPRUCE_PLANKS:  r = 0x6D; g = 0x5E; b = 0x4B; break; // 深木色
        case TEX_SPRUCE_LOG_TOP: r = 0x5D; g = 0x4B; b = 0x3A; break; // 深年轮色
        case TEX_SPRUCE_LOG_SIDE: r = 0x3D; g = 0x2F; b = 0x23; break; // 深树皮色
        case TEX_SAND:           r = 0xE8; g = 0xDB; b = 0xA0; break; // 沙色
        case TEX_GRAVEL:         r = 0x85; g = 0x7F; b = 0x74; break; // 砂砾色
        case TEX_WATER:          r = 0x3F; g = 0x76; b = 0xE4; break; // 蓝色
        case TEX_OAK_LEAVES:     r = 0x47; g = 0xA0; b = 0x36; break; // 树叶绿
        case TEX_SPRUCE_LEAVES:  r = 0x2D; g = 0x6B; b = 0x21; break; // 深树叶绿
        case TEX_GRASS_BLOCK_SNOW: r = 0xF0; g = 0xF0; b = 0xF0; break; // 雪白
        case TEX_SNOW:           r = 0xF0; g = 0xF8; b = 0xFF; break; // 雪白
        case TEX_ICE:            r = 0xA0; g = 0xD8; b = 0xF0; break; // 冰蓝
        case TEX_GRASS_PLANT:    r = 0x5B; g = 0x8E; b = 0x2D; break; // 草绿
        case TEX_GRASS_SIDE_OVERLAY: r = 0x7C; g = 0xB3; b = 0x42; break; // 与 grass_top 相同
        default:                 r = 0xAA; g = 0x44; b = 0xAA; break; // 紫色（未知）
    }
}
