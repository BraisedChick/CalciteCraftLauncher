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

    // ---- 矿石 ----
    TEX_COAL_ORE = 21,                // coal_ore.png
    TEX_DEEPSLATE_COAL_ORE = 22,      // deepslate_coal_ore.png
    TEX_COPPER_ORE = 23,              // copper_ore.png
    TEX_DEEPSLATE_COPPER_ORE = 24,    // deepslate_copper_ore.png
    TEX_DIAMOND_ORE = 25,             // diamond_ore.png
    TEX_DEEPSLATE_DIAMOND_ORE = 26,   // deepslate_diamond_ore.png
    TEX_EMERALD_ORE = 27,             // emerald_ore.png
    TEX_DEEPSLATE_EMERALD_ORE = 28,   // deepslate_emerald_ore.png
    TEX_GOLD_ORE = 29,                // gold_ore.png
    TEX_DEEPSLATE_GOLD_ORE = 30,      // deepslate_gold_ore.png
    TEX_IRON_ORE = 31,                // iron_ore.png
    TEX_DEEPSLATE_IRON_ORE = 32,      // deepslate_iron_ore.png
    TEX_LAPIS_ORE = 33,               // lapis_ore.png
    TEX_DEEPSLATE_LAPIS_ORE = 34,     // deepslate_lapis_ore.png
    TEX_REDSTONE_ORE = 35,            // redstone_ore.png
    TEX_DEEPSLATE_REDSTONE_ORE = 36,  // deepslate_redstone_ore.png
    TEX_NETHER_GOLD_ORE = 37,         // nether_gold_ore.png
    TEX_NETHER_QUARTZ_ORE = 38,       // nether_quartz_ore.png

    // ---- 其他 ----
    TEX_SNOW_BLOCK = 39,              // snow_block.png
    TEX_AMETHYST_BLOCK = 40,          // amethyst_block.png
    TEX_CALCITE = 41,                 // calcite.png

    // ---- 深板岩 ----
    TEX_DEEPSLATE = 42,               // deepslate.png（侧面纹理）
    TEX_DEEPSLATE_TOP = 43,           // deepslate_top.png
    TEX_COBBLED_DEEPSLATE = 44,       // cobbled_deepslate.png
    TEX_POLISHED_DEEPSLATE = 45,      // polished_deepslate.png
    TEX_DEEPSLATE_BRICKS = 46,        // deepslate_bricks.png
    TEX_DEEPSLATE_TILES = 47,         // deepslate_tiles.png
    TEX_CRACKED_DEEPSLATE_BRICKS = 48, // cracked_deepslate_bricks.png
    TEX_CRACKED_DEEPSLATE_TILES = 49,  // cracked_deepslate_tiles.png
    TEX_CHISELED_DEEPSLATE = 50,      // chiseled_deepslate.png

    TEX_DIORITE = 51,                 // diorite.png
    TEX_GRANITE = 52,                 // granite.png
    TEX_TUFF = 53,                    // tuff.png

    TEXTURE_LAYER_COUNT,  // 纹理层总数（当前 = 54）
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
        || name == "dripstone_block") {
        return {TEX_STONE, TEX_STONE, TEX_STONE};
    }

    // 闪长岩
    if (name == "diorite") {
        return {TEX_DIORITE, TEX_DIORITE, TEX_DIORITE};
    }

    // 花岗岩
    if (name == "granite") {
        return {TEX_GRANITE, TEX_GRANITE, TEX_GRANITE};
    }

    // 凝灰岩
    if (name == "tuff") {
        return {TEX_TUFF, TEX_TUFF, TEX_TUFF};
    }

    // 深板岩（顶面和侧面纹理不同）
    if (name == "deepslate" || name == "infested_deepslate") {
        return {TEX_DEEPSLATE_TOP, TEX_DEEPSLATE, TEX_DEEPSLATE};
    }

    // 深板岩变种
    if (name == "cobbled_deepslate") {
        return {TEX_COBBLED_DEEPSLATE, TEX_COBBLED_DEEPSLATE, TEX_COBBLED_DEEPSLATE};
    }
    if (name == "polished_deepslate") {
        return {TEX_POLISHED_DEEPSLATE, TEX_POLISHED_DEEPSLATE, TEX_POLISHED_DEEPSLATE};
    }
    if (name == "deepslate_bricks" || name == "deepslate_brick_slab"
        || name == "deepslate_brick_stairs") {
        return {TEX_DEEPSLATE_BRICKS, TEX_DEEPSLATE_BRICKS, TEX_DEEPSLATE_BRICKS};
    }
    if (name == "deepslate_tiles" || name == "deepslate_tile_slab"
        || name == "deepslate_tile_stairs") {
        return {TEX_DEEPSLATE_TILES, TEX_DEEPSLATE_TILES, TEX_DEEPSLATE_TILES};
    }
    if (name == "cracked_deepslate_bricks") {
        return {TEX_CRACKED_DEEPSLATE_BRICKS, TEX_CRACKED_DEEPSLATE_BRICKS, TEX_CRACKED_DEEPSLATE_BRICKS};
    }
    if (name == "cracked_deepslate_tiles") {
        return {TEX_CRACKED_DEEPSLATE_TILES, TEX_CRACKED_DEEPSLATE_TILES, TEX_CRACKED_DEEPSLATE_TILES};
    }
    if (name == "chiseled_deepslate") {
        return {TEX_CHISELED_DEEPSLATE, TEX_CHISELED_DEEPSLATE, TEX_CHISELED_DEEPSLATE};
    }

    // 紫水晶块
    if (name == "amethyst_block" || name == "budding_amethyst") {
        return {TEX_AMETHYST_BLOCK, TEX_AMETHYST_BLOCK, TEX_AMETHYST_BLOCK};
    }

    // 方解石
    if (name == "calcite") {
        return {TEX_CALCITE, TEX_CALCITE, TEX_CALCITE};
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

    // 雪块（完整方块）
    if (name == "snow_block") {
        return {TEX_SNOW_BLOCK, TEX_SNOW_BLOCK, TEX_SNOW_BLOCK};
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

    // ---- 矿石 ----
    if (name == "coal_ore") {
        return {TEX_COAL_ORE, TEX_COAL_ORE, TEX_COAL_ORE};
    }
    if (name == "deepslate_coal_ore") {
        return {TEX_DEEPSLATE_COAL_ORE, TEX_DEEPSLATE_COAL_ORE, TEX_DEEPSLATE_COAL_ORE};
    }
    if (name == "copper_ore") {
        return {TEX_COPPER_ORE, TEX_COPPER_ORE, TEX_COPPER_ORE};
    }
    if (name == "deepslate_copper_ore") {
        return {TEX_DEEPSLATE_COPPER_ORE, TEX_DEEPSLATE_COPPER_ORE, TEX_DEEPSLATE_COPPER_ORE};
    }
    if (name == "diamond_ore") {
        return {TEX_DIAMOND_ORE, TEX_DIAMOND_ORE, TEX_DIAMOND_ORE};
    }
    if (name == "deepslate_diamond_ore") {
        return {TEX_DEEPSLATE_DIAMOND_ORE, TEX_DEEPSLATE_DIAMOND_ORE, TEX_DEEPSLATE_DIAMOND_ORE};
    }
    if (name == "emerald_ore") {
        return {TEX_EMERALD_ORE, TEX_EMERALD_ORE, TEX_EMERALD_ORE};
    }
    if (name == "deepslate_emerald_ore") {
        return {TEX_DEEPSLATE_EMERALD_ORE, TEX_DEEPSLATE_EMERALD_ORE, TEX_DEEPSLATE_EMERALD_ORE};
    }
    if (name == "gold_ore") {
        return {TEX_GOLD_ORE, TEX_GOLD_ORE, TEX_GOLD_ORE};
    }
    if (name == "deepslate_gold_ore") {
        return {TEX_DEEPSLATE_GOLD_ORE, TEX_DEEPSLATE_GOLD_ORE, TEX_DEEPSLATE_GOLD_ORE};
    }
    if (name == "iron_ore") {
        return {TEX_IRON_ORE, TEX_IRON_ORE, TEX_IRON_ORE};
    }
    if (name == "deepslate_iron_ore") {
        return {TEX_DEEPSLATE_IRON_ORE, TEX_DEEPSLATE_IRON_ORE, TEX_DEEPSLATE_IRON_ORE};
    }
    if (name == "lapis_ore") {
        return {TEX_LAPIS_ORE, TEX_LAPIS_ORE, TEX_LAPIS_ORE};
    }
    if (name == "deepslate_lapis_ore") {
        return {TEX_DEEPSLATE_LAPIS_ORE, TEX_DEEPSLATE_LAPIS_ORE, TEX_DEEPSLATE_LAPIS_ORE};
    }
    if (name == "redstone_ore") {
        return {TEX_REDSTONE_ORE, TEX_REDSTONE_ORE, TEX_REDSTONE_ORE};
    }
    if (name == "deepslate_redstone_ore") {
        return {TEX_DEEPSLATE_REDSTONE_ORE, TEX_DEEPSLATE_REDSTONE_ORE, TEX_DEEPSLATE_REDSTONE_ORE};
    }
    if (name == "nether_gold_ore") {
        return {TEX_NETHER_GOLD_ORE, TEX_NETHER_GOLD_ORE, TEX_NETHER_GOLD_ORE};
    }
    if (name == "nether_quartz_ore") {
        return {TEX_NETHER_QUARTZ_ORE, TEX_NETHER_QUARTZ_ORE, TEX_NETHER_QUARTZ_ORE};
    }

    // 未知方块：根据 blockState ID 取模分配纹理，避免全部显示为石头
    int texIndex = TEX_STONE + (blockState % 10);
    if (texIndex >= TEXTURE_LAYER_COUNT) texIndex = TEX_STONE;
    return {texIndex, texIndex, texIndex};
}

// 判断一个 blockState 是否为完整方块（不透明、遮挡相邻面）
inline bool isFullBlock(int32_t blockState) {
    if (blockState == 0) return false;
    return BlockRegistry::getInstance().getBlockMetadata(blockState).isFullBlock;
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
        case TEX_SNOW_BLOCK:     return "powder_snow.png";
        case TEX_AMETHYST_BLOCK: return "amethyst_block.png";
        case TEX_CALCITE:        return "calcite.png";
        case TEX_ICE:            return "ice.png";
        case TEX_GRASS_PLANT:    return "grass.png";
        case TEX_GRASS_SIDE_OVERLAY: return "grass_block_side_overlay.png";
        case TEX_COAL_ORE:           return "coal_ore.png";
        case TEX_DEEPSLATE_COAL_ORE: return "deepslate_coal_ore.png";
        case TEX_COPPER_ORE:         return "copper_ore.png";
        case TEX_DEEPSLATE_COPPER_ORE: return "deepslate_copper_ore.png";
        case TEX_DIAMOND_ORE:        return "diamond_ore.png";
        case TEX_DEEPSLATE_DIAMOND_ORE: return "deepslate_diamond_ore.png";
        case TEX_EMERALD_ORE:        return "emerald_ore.png";
        case TEX_DEEPSLATE_EMERALD_ORE: return "deepslate_emerald_ore.png";
        case TEX_GOLD_ORE:           return "gold_ore.png";
        case TEX_DEEPSLATE_GOLD_ORE: return "deepslate_gold_ore.png";
        case TEX_IRON_ORE:           return "iron_ore.png";
        case TEX_DEEPSLATE_IRON_ORE: return "deepslate_iron_ore.png";
        case TEX_LAPIS_ORE:          return "lapis_ore.png";
        case TEX_DEEPSLATE_LAPIS_ORE: return "deepslate_lapis_ore.png";
        case TEX_REDSTONE_ORE:       return "redstone_ore.png";
        case TEX_DEEPSLATE_REDSTONE_ORE: return "deepslate_redstone_ore.png";
        case TEX_NETHER_GOLD_ORE:    return "nether_gold_ore.png";
        case TEX_NETHER_QUARTZ_ORE:  return "nether_quartz_ore.png";
        // 深板岩
        case TEX_DEEPSLATE:                return "deepslate.png";
        case TEX_DEEPSLATE_TOP:            return "deepslate_top.png";
        case TEX_COBBLED_DEEPSLATE:        return "cobbled_deepslate.png";
        case TEX_POLISHED_DEEPSLATE:       return "polished_deepslate.png";
        case TEX_DEEPSLATE_BRICKS:         return "deepslate_bricks.png";
        case TEX_DEEPSLATE_TILES:          return "deepslate_tiles.png";
        case TEX_CRACKED_DEEPSLATE_BRICKS: return "cracked_deepslate_bricks.png";
        case TEX_CRACKED_DEEPSLATE_TILES:  return "cracked_deepslate_tiles.png";
        case TEX_CHISELED_DEEPSLATE:       return "chiseled_deepslate.png";
        case TEX_DIORITE:                  return "diorite.png";
        case TEX_GRANITE:                  return "granite.png";
        case TEX_TUFF:                     return "tuff.png";
        default:                     return "stone.png";
    }
}

// ============================================================
// 获取第 i 层的占位纹理颜色（当 PNG 文件不存在时使用）
// ============================================================
inline void getPlaceholderColor(int /*layer*/, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = 0xAA; g = 0x44; b = 0xAA; // 紫色（缺少纹理时的占位色）
}
