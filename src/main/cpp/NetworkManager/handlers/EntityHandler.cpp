#include "NetworkManager/NetworkManager.h"
#include "utils.h"
#include "EntityManager.h"
#include "Entity.h"

#include "protocolCraft/Packets/Game/Clientbound/ClientboundAddEntityPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundAddMobPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundAddPlayerPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundMoveEntityPacketPosRot.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundMoveEntityPacketPos.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundMoveEntityPacketRot.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundTeleportEntityPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundRemoveEntitiesPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSetEntityMotionPacket.hpp"

void NetworkManager::handleEntity(int packetId, const std::vector<uint8_t>& data, size_t startPos) {
    switch (packetId) {
#if PROTOCOL_VERSION >= 762
        case 0x00: // BundlePacket - 跳过
            break;
#endif

#if PROTOCOL_VERSION < 762
        case 0x00:
#else
        case 0x01:
#endif
        { // Spawn Entity
            if (data.size() <= startPos + 1) break;
            ProtocolCraft::ClientboundAddEntityPacket pkt;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            pkt.Read(iter, len);
            Entity e;
            e.entityId = pkt.GetEntityId();
            e.protocolTypeId = pkt.GetType();
            e.type = entityTypeFromProtocolId(pkt.GetType());
            LOGI("SpawnEntity: entityId=%d, typeId=%d, type=%s",
                 e.entityId, e.protocolTypeId, e.getTypeName());
            e.x = pkt.GetX();
            e.y = pkt.GetY();
            e.z = pkt.GetZ();
            e.yaw = pkt.GetYRot() * 360.0f / 256.0f;
            e.pitch = pkt.GetXRot() * 360.0f / 256.0f;
            e.headYaw = e.yaw;
            EntityManager::getInstance().addEntity(e);
            break;
        }

#if PROTOCOL_VERSION < 759
        case 0x02: { // Spawn Mob
            ProtocolCraft::ClientboundAddMobPacket pkt;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            pkt.Read(iter, len);
            Entity e;
            e.entityId = pkt.GetEntityId();
            e.protocolTypeId = pkt.GetType();
            e.type = entityTypeFromProtocolId(pkt.GetType());
            e.x = pkt.GetX();
            e.y = pkt.GetY();
            e.z = pkt.GetZ();
            e.yaw = pkt.GetYRot() * 360.0f / 256.0f;
            e.pitch = pkt.GetXRot() * 360.0f / 256.0f;
            e.headYaw = pkt.GetYHeadRot() * 360.0f / 256.0f;
            EntityManager::getInstance().addEntity(e);
            break;
        }
#endif

#if PROTOCOL_VERSION < 764
#if PROTOCOL_VERSION < 762
        case 0x04:
#else
        case 0x03:
#endif
        { // Spawn Player
            ProtocolCraft::ClientboundAddPlayerPacket pkt;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            pkt.Read(iter, len);
            Entity e;
            e.entityId = pkt.GetEntityId();
            e.type = EntityType::PLAYER;
            e.x = pkt.GetX();
            e.y = pkt.GetY();
            e.z = pkt.GetZ();
            e.yaw = pkt.GetYRot() * 360.0f / 256.0f;
            e.pitch = pkt.GetXRot() * 360.0f / 256.0f;
            e.headYaw = e.yaw;
            EntityManager::getInstance().addEntity(e);
            break;
        }
#endif

#if PROTOCOL_VERSION < 762
        case 0x2A:
#else
        case 0x2C:
#endif
        { // Entity Position and Rotation
            ProtocolCraft::ClientboundMoveEntityPacketPosRot pkt;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            pkt.Read(iter, len);
            float yaw = pkt.GetYRot() * 360.0f / 256.0f;
            float pitch = pkt.GetXRot() * 360.0f / 256.0f;
            EntityManager::getInstance().moveEntityRot(
                pkt.GetEntityId(), pkt.GetXA(), pkt.GetYA(), pkt.GetZA(), yaw, pitch);
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x29:
#else
        case 0x2B:
#endif
        { // Entity Position
            ProtocolCraft::ClientboundMoveEntityPacketPos pkt;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            pkt.Read(iter, len);
            EntityManager::getInstance().moveEntity(pkt.GetEntityId(), pkt.GetXA(), pkt.GetYA(), pkt.GetZA());
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x2B:
#else
        case 0x2D:
#endif
        { // Entity Rotation
            ProtocolCraft::ClientboundMoveEntityPacketRot pkt;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            pkt.Read(iter, len);
            float yaw = pkt.GetYRot() * 360.0f / 256.0f;
            float pitch = pkt.GetXRot() * 360.0f / 256.0f;
            EntityManager::getInstance().rotateEntity(pkt.GetEntityId(), yaw, pitch);
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x62:
#else
        case 0x68:
#endif
        { // Teleport Entity
            ProtocolCraft::ClientboundTeleportEntityPacket pkt;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            pkt.Read(iter, len);
            float yaw = pkt.GetYRot() * 360.0f / 256.0f;
            float pitch = pkt.GetXRot() * 360.0f / 256.0f;
            EntityManager::getInstance().teleportEntity(
                pkt.GetEntityId(), pkt.GetX(), pkt.GetY(), pkt.GetZ(), yaw, pitch);
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x3A:
#else
        case 0x3E:
#endif
        { // Remove Entities
            ProtocolCraft::ClientboundRemoveEntitiesPacket pkt;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            pkt.Read(iter, len);
            const auto& ids = pkt.GetEntityIds();
            for (auto id : ids) {
                EntityManager::getInstance().removeEntity((int)id);
            }
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x4F:
#else
        case 0x54:
#endif
        { // Set Entity Motion
            ProtocolCraft::ClientboundSetEntityMotionPacket pkt;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            pkt.Read(iter, len);
            EntityManager::getInstance().setEntityMotion(
                pkt.GetEntityId(), pkt.GetXA(), pkt.GetYA(), pkt.GetZA());
            break;
        }

        default:
            break;
    }
}
