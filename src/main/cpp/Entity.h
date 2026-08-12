#pragma once

#include <cstdint>
#include <string>

// 实体类型枚举（基于 1.18.2 官方注册顺序）
enum class EntityType {
    // 按协议 ID 顺序排列，最后一个 FISHING_BOBBER 为 112
    AREA_EFFECT_CLOUD,
    ARMOR_STAND,
    ARROW,
    AXOLOTL,
    BAT,
    BEE,
    BLAZE,
    BOAT,
    CAT,
    CAVE_SPIDER,
    CHICKEN,
    COD,
    COW,
    CREEPER,
    DOLPHIN,
    DONKEY,
    DRAGON_FIREBALL,
    DROWNED,
    ELDER_GUARDIAN,
    END_CRYSTAL,
    ENDER_DRAGON,
    ENDERMAN,
    ENDERMITE,
    EVOKER,
    EVOKER_FANGS,
    EXPERIENCE_ORB,
    EYE_OF_ENDER,
    FALLING_BLOCK,
    FIREWORK_ROCKET,
    FOX,
    GHAST,
    GIANT,
    GLOW_ITEM_FRAME,
    GLOW_SQUID,
    GOAT,
    GUARDIAN,
    HOGLIN,
    HORSE,
    HUSK,
    ILLUSIONER,
    IRON_GOLEM,
    ITEM,
    ITEM_FRAME,
    FIREBALL,
    LEASH_KNOT,
    LIGHTNING_BOLT,
    LLAMA,
    LLAMA_SPIT,
    MAGMA_CUBE,
    MARKER,
    MINECART,
    CHEST_MINECART,
    COMMAND_BLOCK_MINECART,
    FURNACE_MINECART,
    HOPPER_MINECART,
    SPAWNER_MINECART,
    TNT_MINECART,
    MULE,
    MOOSHROOM,
    OCELOT,
    PAINTING,
    PANDA,
    PARROT,
    PHANTOM,
    PIG,
    PIGLIN,
    PIGLIN_BRUTE,
    PILLAGER,
    POLAR_BEAR,
    TNT,
    PUFFERFISH,
    RABBIT,
    RAVAGER,
    SALMON,
    SHEEP,
    SHULKER,
    SHULKER_BULLET,
    SILVERFISH,
    SKELETON,
    SKELETON_HORSE,
    SLIME,
    SMALL_FIREBALL,
    SNOW_GOLEM,
    SNOWBALL,
    SPECTRAL_ARROW,
    SPIDER,
    SQUID,
    STRAY,
    STRIDER,
    EGG,
    ENDER_PEARL,
    EXPERIENCE_BOTTLE,
    POTION,
    TRIDENT,
    TRADER_LLAMA,
    TROPICAL_FISH,
    TURTLE,
    VEX,
    VILLAGER,
    VINDICATOR,
    WANDERING_TRADER,
    WITCH,
    WITHER,
    WITHER_SKELETON,
    WITHER_SKULL,
    WOLF,
    ZOGLIN,
    ZOMBIE,
    ZOMBIE_HORSE,
    ZOMBIE_VILLAGER,
    ZOMBIFIED_PIGLIN,
    PLAYER,
    FISHING_BOBBER,
    UNKNOWN,
    // 便于迭代，总是最后一个
    COUNT
};

// 从 MC 协议实体类型 ID 映射到 EntityType（1.18.2 / Protocol 758）
EntityType entityTypeFromProtocolId(int typeId);

// 实体数据（对应 MC 的 Entity.java）
struct Entity {
    int entityId = 0;
    EntityType type = EntityType::AREA_EFFECT_CLOUD; // 默认设为有效类型，避免 UNKNOWN
    int protocolTypeId = 0;

    double x = 0, y = 0, z = 0;
    float yaw = 0;
    float pitch = 0;
    float headYaw = 0;
    float bodyYaw= 0;

    double vx = 0, vy = 0, vz = 0;

    double prevX = 0, prevY = 0, prevZ = 0;

    int tickCount = 0;
    bool onGround = false;
    bool removed = false;

    std::string uuid;

    const char* getTypeName() const;
};