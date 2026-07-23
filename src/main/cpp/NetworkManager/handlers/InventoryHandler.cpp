#include "NetworkManager/NetworkManager.h"
#include "ClientEngine/GameEngine.h"
#include "ClientEngine/ClientEngine.h"
#include "utils.h"
#include "PlayerInventory.h"
#include "BlockRegistry.h"
#include "gui/GameUI.h"

#include "protocolCraft/Packets/Game/Clientbound/ClientboundContainerSetContentPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundContainerSetSlotPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundOpenScreenPacket.hpp"

// 从物品 NBT 中提取耐久损耗值
static int extractItemDamage(const ProtocolCraft::Slot& slot) {
#if PROTOCOL_VERSION < 766
    if (!slot.IsEmptySlot()) {
        const auto& nbt = slot.GetNbt();
        if (nbt.is<ProtocolCraft::NBT::TagCompound>() && nbt.contains("Damage")) {
            try {
                return nbt["Damage"].get<ProtocolCraft::NBT::TagInt>();
            } catch (...) {}
        }
    }
#endif
    return 0;
}

// 根据物品名称获取最大耐久
static int getMaxDurability(const std::string& itemName) {
    static const std::unordered_map<std::string, int> durabilityMap = {
        {"wooden_sword", 59}, {"wooden_pickaxe", 59}, {"wooden_axe", 59},
        {"wooden_shovel", 59}, {"wooden_hoe", 59},
        {"stone_sword", 131}, {"stone_pickaxe", 131}, {"stone_axe", 131},
        {"stone_shovel", 131}, {"stone_hoe", 131},
        {"iron_sword", 250}, {"iron_pickaxe", 250}, {"iron_axe", 250},
        {"iron_shovel", 250}, {"iron_hoe", 250},
        {"golden_sword", 32}, {"golden_pickaxe", 32}, {"golden_axe", 32},
        {"golden_shovel", 32}, {"golden_hoe", 32},
        {"diamond_sword", 1561}, {"diamond_pickaxe", 1561}, {"diamond_axe", 1561},
        {"diamond_shovel", 1561}, {"diamond_hoe", 1561},
        {"netherite_sword", 2031}, {"netherite_pickaxe", 2031}, {"netherite_axe", 2031},
        {"netherite_shovel", 2031}, {"netherite_hoe", 2031},
        {"bow", 384}, {"crossbow", 326}, {"trident", 250},
        {"fishing_rod", 64}, {"flint_and_steel", 64},
        {"shears", 238}, {"shield", 336},
        {"elytra", 432}, {"turtle_helmet", 275},
        {"leather_helmet", 55}, {"chainmail_helmet", 165},
        {"iron_helmet", 165}, {"golden_helmet", 77},
        {"diamond_helmet", 363}, {"netherite_helmet", 407},
        {"leather_chestplate", 80}, {"chainmail_chestplate", 240},
        {"iron_chestplate", 240}, {"golden_chestplate", 112},
        {"diamond_chestplate", 528}, {"netherite_chestplate", 592},
        {"leather_leggings", 75}, {"chainmail_leggings", 225},
        {"iron_leggings", 225}, {"golden_leggings", 105},
        {"diamond_leggings", 495}, {"netherite_leggings", 555},
        {"leather_boots", 65}, {"chainmail_boots", 195},
        {"iron_boots", 195}, {"golden_boots", 91},
        {"diamond_boots", 429}, {"netherite_boots", 481},
        {"leather_horse_armor", 80}, {"iron_horse_armor", 140},
        {"golden_horse_armor", 140}, {"diamond_horse_armor", 280},
    };
    auto it = durabilityMap.find(itemName);
    return (it != durabilityMap.end()) ? it->second : 0;
}

void NetworkManager::handleInventory(int packetId, const std::vector<uint8_t>& data, size_t startPos) {
    switch (packetId) {
#if PROTOCOL_VERSION < 762
        case 0x14:
#else
        case 0x12:
#endif
        { // Container Set Content
            ProtocolCraft::ClientboundContainerSetContentPacket containerPacket;
            std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();
            containerPacket.Read(iter, length);

            int containerId = containerPacket.GetContainerId();
            const auto& items = containerPacket.GetItems();

            std::vector<InvSlot> invSlots;
            invSlots.reserve(items.size());
            int nonEmptyCount = 0;
            for (const auto& slot : items) {
                InvSlot is;
                is.present = !slot.IsEmptySlot();
                if (is.present) {
                    is.itemId = slot.GetItemId();
                    is.count = slot.GetItemCount();
                    is.damage = extractItemDamage(slot);
                    if (is.damage > 0) {
                        is.maxDamage = getMaxDurability(ClientEngine::getInstance()->getBlockRegistry()->getItemName(is.itemId));
                    }
                    nonEmptyCount++;
                }
                invSlots.push_back(is);
            }

            m_engine->getInventory()->setContent(containerId, invSlots);
            m_engine->getInventory()->setStateId(containerPacket.GetStateId());

            const auto& carried = containerPacket.GetCarriedItem();
            InvSlot cursorIs;
            cursorIs.present = !carried.IsEmptySlot();
            if (cursorIs.present) {
                cursorIs.itemId = carried.GetItemId();
                cursorIs.count = carried.GetItemCount();
            }
            m_engine->getInventory()->setCursorItem(cursorIs);

            LOGI("Container Set Content: id=%d, state=%d, slots=%zu, nonEmpty=%d",
                 containerId, containerPacket.GetStateId(), items.size(), nonEmptyCount);
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x16:
#else
        case 0x14:
#endif
        { // Container Set Slot
            ProtocolCraft::ClientboundContainerSetSlotPacket slotPacket;
            std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();
            slotPacket.Read(iter, length);

            int containerId = slotPacket.GetContainerId();
            int slotIndex = slotPacket.GetSlot();
            const auto& pcSlot = slotPacket.GetItemStack();

            InvSlot is;
            is.present = !pcSlot.IsEmptySlot();
            if (is.present) {
                is.itemId = pcSlot.GetItemId();
                is.count = pcSlot.GetItemCount();
                is.damage = extractItemDamage(pcSlot);
                if (is.damage > 0) {
                    is.maxDamage = getMaxDurability(ClientEngine::getInstance()->getBlockRegistry()->getItemName(is.itemId));
                }
            }

            m_engine->getInventory()->setSlot(containerId, slotIndex, is);
            m_engine->getInventory()->setStateId(slotPacket.GetStateId());
            LOGI("Container Set Slot: id=%d, slot=%d, present=%d, itemId=%d, count=%d",
                 containerId, slotIndex, is.present, is.itemId, is.count);
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x2E:
#else
        case 0x30:
#endif
        { // Open Screen
            ProtocolCraft::ClientboundOpenScreenPacket pkt;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            pkt.Read(iter, len);
            int containerId = pkt.GetContainerId();
            int containerType = pkt.GetType();
            LOGI("OpenScreen: containerId=%d, type=%d", containerId, containerType);
            GameUI::getInstance().openContainer(containerId, containerType);
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x13:
#else
        case 0x11:
#endif
        { // Container Close
            GameUI::getInstance().closeContainer();
            LOGI("Container closed by server");
            break;
        }

        default:
            break;
    }
}
