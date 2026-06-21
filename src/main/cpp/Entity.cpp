#include "Entity.h"

// Protocol 758 (1.18.2) 实体类型 ID 映射
// 这些 ID 是服务器 RegistryHolder 注册的，此处提供默认映射
// 后续可从 Login 包的 registry_holder 动态解析
EntityType entityTypeFromProtocolId(int typeId) {
    switch (typeId) {
        // 常见生物
        case 1:  return EntityType::AREA_EFFECT_CLOUD;
        case 2:  return EntityType::ARMOR_STAND;
        case 3:  return EntityType::ARROW;
        case 5:  return EntityType::BLAZE;
        case 7:  return EntityType::BOAT;
        case 10: return EntityType::CAT;
        case 14: return EntityType::CHICKEN;
        case 15: return EntityType::COD;
        case 21: return EntityType::COW;
        case 22: return EntityType::CREEPER;
        case 26: return EntityType::ENDERMAN;
        case 31: return EntityType::EXPERIENCE_ORB;
        case 34: return EntityType::GHAST;
        case 39: return EntityType::HOGLIN;
        case 42: return EntityType::IRON_GOLEM;
        case 45: return EntityType::ITEM;
        case 57: return EntityType::MINECART;
        case 65: return EntityType::PIG;
        case 67: return EntityType::PIGLIN;
        case 81: return EntityType::SHEEP;
        case 82: return EntityType::SKELETON;
        case 83: return EntityType::SLIME;
        case 87: return EntityType::SPIDER;
        case 99: return EntityType::VILLAGER;
        case 102: return EntityType::WITCH;
        case 104: return EntityType::WOLF;
        case 112: return EntityType::ZOMBIE;
        case 113: return EntityType::ZOMBIE_VILLAGER;
        default: return EntityType::UNKNOWN;
    }
}

const char* Entity::getTypeName() const {
    switch (type) {
        case EntityType::ZOMBIE: return "zombie";
        case EntityType::SKELETON: return "skeleton";
        case EntityType::CREEPER: return "creeper";
        case EntityType::SPIDER: return "spider";
        case EntityType::ENDERMAN: return "enderman";
        case EntityType::PIG: return "pig";
        case EntityType::COW: return "cow";
        case EntityType::SHEEP: return "sheep";
        case EntityType::CHICKEN: return "chicken";
        case EntityType::HORSE: return "horse";
        case EntityType::PLAYER: return "player";
        case EntityType::ARROW: return "arrow";
        case EntityType::ITEM: return "item";
        case EntityType::EXPERIENCE_ORB: return "experience_orb";
        case EntityType::FALLING_BLOCK: return "falling_block";
        case EntityType::TNT: return "tnt";
        case EntityType::BOAT: return "boat";
        case EntityType::MINECART: return "minecart";
        case EntityType::SLIME: return "slime";
        case EntityType::WITCH: return "witch";
        case EntityType::BLAZE: return "blaze";
        case EntityType::GHAST: return "ghast";
        case EntityType::ZOMBIE_VILLAGER: return "zombie_villager";
        case EntityType::WOLF: return "wolf";
        case EntityType::CAT: return "cat";
        case EntityType::VILLAGER: return "villager";
        case EntityType::IRON_GOLEM: return "iron_golem";
        case EntityType::PIGLIN: return "piglin";
        case EntityType::HOGLIN: return "hoglin";
        case EntityType::WARDEN: return "warden";
        case EntityType::ALLAY: return "allay";
        default: return "unknown";
    }
}
