#pragma once

#include <cstdint>
#include <string>

// 实体类型枚举（对应 MC RegistryName → EntityType）
enum class EntityType {
    UNKNOWN = 0,
    // 生物
    ZOMBIE, SKELETON, CREEPER, SPIDER, ENDERMAN,
    PIG, COW, SHEEP, CHICKEN, HORSE,
    // 玩家
    PLAYER,
    // 投射物/物品
    ARROW, ITEM, EXPERIENCE_ORB,
    // 其他
    FALLING_BLOCK, TNT, BOAT, MINECART,
    SLIME, WITCH, BLAZE, GHAST, ZOMBIE_VILLAGER,
    WOLF, CAT, VILLAGER, IRON_GOLEM,
    PIGLIN, HOGLIN, WARDEN, ALLAY,
    AREA_EFFECT_CLOUD, ARMOR_STAND, COD, HUSK,
    COUNT
};

// 从 MC 协议实体类型 ID 映射到 EntityType（1.18.2 / Protocol 758）
EntityType entityTypeFromProtocolId(int typeId);

// 实体数据（对应 MC 的 Entity.java）
struct Entity {
    int entityId = 0;
    EntityType type = EntityType::UNKNOWN;
    int protocolTypeId = 0;

    // 位置（双精度，与 MC 协议一致）
    double x = 0, y = 0, z = 0;
    // 旋转（角度，MC 用 byte 0-255 映射 0-360°）
    float yaw = 0;     // Y rotation (body)
    float pitch = 0;   // X rotation (head up/down)
    float headYaw = 0; // Head Y rotation

    // 速度（用于平滑插值）
    double vx = 0, vy = 0, vz = 0;

    // 渲染用插值（上一帧位置，用于平滑移动）
    double prevX = 0, prevY = 0, prevZ = 0;
    float prevYaw = 0, prevHeadYaw = 0;

    // 时间戳
    int tickCount = 0;
    bool onGround = false;
    bool removed = false;

    // UUID (玩家实体使用)
    std::string uuid;

    // 获取实体名称（用于纹理查找）
    const char* getTypeName() const;
};
