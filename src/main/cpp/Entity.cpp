#include "Entity.h"

// 1.18.2 EntityType.java 注册表顺序（注册顺序 = 协议 ID）
EntityType entityTypeFromProtocolId(int typeId) {
    switch (typeId) {
        // 以下为常用实体，完整表见 EntityType.java L156-268
        case 2:   return EntityType::ARROW;
        case 6:   return EntityType::BLAZE;
        case 7:   return EntityType::BOAT;
        case 8:   return EntityType::CAT;
        case 10:  return EntityType::CHICKEN;
        case 11:  return EntityType::COD;
        case 12:  return EntityType::COW;
        case 13:  return EntityType::CREEPER;
        case 21:  return EntityType::ENDERMAN;
        case 25:  return EntityType::EXPERIENCE_ORB;
        case 27:  return EntityType::FALLING_BLOCK;
        case 30:  return EntityType::GHAST;
        case 36:  return EntityType::HOGLIN;
        case 37:  return EntityType::HORSE;
        case 38:  return EntityType::HUSK;
        case 40:  return EntityType::IRON_GOLEM;
        case 41:  return EntityType::ITEM;
        case 50:  return EntityType::MINECART;
        case 64:  return EntityType::PIG;
        case 65:  return EntityType::PIGLIN;
        case 69:  return EntityType::TNT;
        case 82:  return EntityType::SHEEP;
        case 84:  return EntityType::SKELETON;
        case 86:  return EntityType::SLIME;
        case 91:  return EntityType::SPIDER;
        case 89:  return EntityType::SKELETON;     // stray
        case 99:  return EntityType::VILLAGER;
        case 107: return EntityType::WITCH;
        case 111: return EntityType::WOLF;
        case 113: return EntityType::ZOMBIE;
        case 115: return EntityType::ZOMBIE_VILLAGER;
        default:  return EntityType::UNKNOWN;
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
