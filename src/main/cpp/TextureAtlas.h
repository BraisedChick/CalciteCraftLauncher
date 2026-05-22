#pragma once

#include <cstdint>
#include <string>
#include <android/log.h>
#include "BlockRegistry.h"

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
    if (name == "minecraft:grass_block") {
        return {TEX_GRASS_TOP, TEX_GRASS_SIDE, TEX_DIRT};
    }

    // 泥土：全部 dirt
    if (name == "minecraft:dirt" || name == "minecraft:coarse_dirt"
        || name == "minecraft:rooted_dirt" || name == "minecraft:mud") {
        return {TEX_DIRT, TEX_DIRT, TEX_DIRT};
    }

    // 石头
    if (name == "minecraft:stone" || name == "minecraft:andesite"
        || name == "minecraft:diorite" || name == "minecraft:granite"
        || name == "minecraft:deepslate" || name == "minecraft:tuff"
        || name == "minecraft:calcite" || name == "minecraft:dripstone_block") {
        return {TEX_STONE, TEX_STONE, TEX_STONE};
    }

    // 圆石
    if (name == "minecraft:cobblestone" || name == "minecraft:mossy_cobblestone"
        || name == "minecraft:stone_bricks" || name == "minecraft:cracked_stone_bricks"
        || name == "minecraft:mossy_stone_bricks") {
        return {TEX_COBBLESTONE, TEX_COBBLESTONE, TEX_COBBLESTONE};
    }

    // 橡木木板
    if (name == "minecraft:oak_planks" || name == "minecraft:oak_stairs"
        || name == "minecraft:oak_slab" || name == "minecraft:oak_fence") {
        return {TEX_OAK_PLANKS, TEX_OAK_PLANKS, TEX_OAK_PLANKS};
    }

    // 橡木原木：顶面原木顶部，侧面原木侧面
    if (name == "minecraft:oak_log" || name == "minecraft:oak_wood"
        || name == "minecraft:stripped_oak_log" || name == "minecraft:stripped_oak_wood") {
        return {TEX_OAK_LOG_TOP, TEX_OAK_LOG_SIDE, TEX_OAK_LOG_TOP};
    }

    // 云杉木板
    if (name == "minecraft:spruce_planks" || name == "minecraft:spruce_stairs"
        || name == "minecraft:spruce_slab" || name == "minecraft:spruce_fence") {
        return {TEX_SPRUCE_PLANKS, TEX_SPRUCE_PLANKS, TEX_SPRUCE_PLANKS};
    }

    // 云杉原木
    if (name == "minecraft:spruce_log" || name == "minecraft:spruce_wood"
        || name == "minecraft:stripped_spruce_log" || name == "minecraft:stripped_spruce_wood") {
        return {TEX_SPRUCE_LOG_TOP, TEX_SPRUCE_LOG_SIDE, TEX_SPRUCE_LOG_TOP};
    }

    // 沙子
    if (name == "minecraft:sand" || name == "minecraft:red_sand"
        || name == "minecraft:sandstone" || name == "minecraft:red_sandstone") {
        return {TEX_SAND, TEX_SAND, TEX_SAND};
    }

    // 砂砾
    if (name == "minecraft:gravel") {
        return {TEX_GRAVEL, TEX_GRAVEL, TEX_GRAVEL};
    }

    // 橡树树叶
    if (name == "minecraft:oak_leaves" || name == "minecraft:birch_leaves"
        || name == "minecraft:jungle_leaves" || name == "minecraft:acacia_leaves"
        || name == "minecraft:dark_oak_leaves" || name == "minecraft:azalea_leaves"
        || name == "minecraft:flowering_azalea_leaves") {
        return {TEX_OAK_LEAVES, TEX_OAK_LEAVES, TEX_OAK_LEAVES};
    }

    // 云杉树叶
    if (name == "minecraft:spruce_leaves") {
        return {TEX_SPRUCE_LEAVES, TEX_SPRUCE_LEAVES, TEX_SPRUCE_LEAVES};
    }

    // 雪草方块：带雪的草方块
    if (name == "minecraft:grass_block" || name.find("snow") != std::string::npos) {
        // 注意：这里和上面的 grass_block 重复了，但 grass_block 已经先返回了
        // 只用于带雪的方块
    }

    // 默认：石头
    return {TEX_STONE, TEX_STONE, TEX_STONE};
}

// ============================================================
// 获取第 i 层的纹理文件名（用于加载）
// ============================================================
inline std::string getTextureFileName(int layer) {
    switch (layer) {
        case TEX_GRASS_TOP:      return "grass_top.png";
        case TEX_GRASS_SIDE:     return "grass_side.png";
        case TEX_DIRT:           return "dirt.png";
        case TEX_STONE:          return "stone.png";
        case TEX_COBBLESTONE:    return "cobblestone.png";
        case TEX_OAK_PLANKS:     return "oak_planks.png";
        case TEX_OAK_LOG_TOP:    return "oak_log_top.png";
        case TEX_OAK_LOG_SIDE:   return "oak_log_side.png";
        case TEX_SPRUCE_PLANKS:  return "spruce_planks.png";
        case TEX_SPRUCE_LOG_TOP: return "spruce_log_top.png";
        case TEX_SPRUCE_LOG_SIDE: return "spruce_log_side.png";
        case TEX_SAND:           return "sand.png";
        case TEX_GRAVEL:         return "gravel.png";
        case TEX_WATER:          return "water.png";
        case TEX_OAK_LEAVES:     return "oak_leaves.png";
        case TEX_SPRUCE_LEAVES:  return "spruce_leaves.png";
        case TEX_GRASS_BLOCK_SNOW: return "grass_block_snow.png";
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
        default:                 r = 0xAA; g = 0x44; b = 0xAA; break; // 紫色（未知）
    }
}
