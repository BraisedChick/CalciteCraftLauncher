#include "NetworkManager/NetworkManager.h"
#include "ClientEngine/GameEngine.h"
#include "utils.h"
#include "Collision.h"
#include "CameraController.h"
#include "Light.h"
#include "PlayerInventory.h"

#include "protocolCraft/Packets/Game/Clientbound/ClientboundKeepAlivePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundPlayerPositionPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSetExperiencePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSetHealthPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSetTimePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSetCarriedItemPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundPlayerCombatKillPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundAcceptTeleportationPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundMovePlayerPacketPosRot.hpp"

void NetworkManager::handlePlayerStatus(int packetId, const std::vector<uint8_t>& data, size_t startPos) {
    switch (packetId) {
#if PROTOCOL_VERSION >= 762
        case 0x00: // BundlePacket - 跳过
            break;
#endif

#if PROTOCOL_VERSION < 762
        case 0x21:
#else
        case 0x23:
#endif
        { // Keep Alive
            if (data.size() - startPos >= 8) {
                long long keepAliveId = 0;
                for (int i = 0; i < 8; i++) {
                    keepAliveId = (keepAliveId << 8) | data[startPos + i];
                }

                std::vector<uint8_t> response;
#if PROTOCOL_VERSION >= 762
                response.push_back(0x12);
#else
                response.push_back(0x0F);
#endif
                for (int i = 7; i >= 0; i--) {
                    response.push_back((keepAliveId >> (i * 8)) & 0xFF);
                }

                bool sent = sendPacket(response);
                if (!sent) {
                    LOGE("Failed to send KeepAlive response!");
                }
            }
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x38:
#else
        case 0x3C:
#endif
        { // Player Position And Look
            ProtocolCraft::ClientboundPlayerPositionPacket posPacket;
            std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();

            posPacket.Read(iter, length);

            m_engine->playerX = posPacket.GetX();
            m_engine->playerY = posPacket.GetY();
            m_engine->playerZ = posPacket.GetZ();
            m_engine->yaw = glm::radians(posPacket.GetYRot());
            m_engine->pitch = glm::radians(posPacket.GetXRot());
            m_engine->hasPosition = true;

            {
                glm::vec3 curPos = Collision::getInstance().getPosition();
                glm::vec3 curVel = Collision::getInstance().getVelocity();
                float diffX = curPos.x - m_engine->playerX;
                float diffY = curPos.y - m_engine->playerY;
                float diffZ = curPos.z - m_engine->playerZ;
                float dist = sqrtf(diffX * diffX + diffY * diffY + diffZ * diffZ);
                LOGI("Received teleport request, ID=%d, server=(%.3f, %.3f, %.3f), client=(%.3f, %.3f, %.3f), dist=%.3f",
                     posPacket.GetId_(),
                     m_engine->playerX, m_engine->playerY, m_engine->playerZ,
                     curPos.x, curPos.y, curPos.z,
                     dist);
            }

            CameraController::getInstance().setPosition(m_engine->playerX, m_engine->playerY, m_engine->playerZ);
            CameraController::getInstance().setRotation(m_engine->pitch, m_engine->yaw);
            Collision::getInstance().setPosition(m_engine->playerX, m_engine->playerY, m_engine->playerZ);

            m_engine->lastSent.x = m_engine->playerX;
            m_engine->lastSent.y = m_engine->playerY;
            m_engine->lastSent.z = m_engine->playerZ;
            m_engine->lastSent.yaw = m_engine->yaw;
            m_engine->lastSent.pitch = m_engine->pitch;
            m_engine->lastSent.onGround = true;
            m_engine->lastSent.initialized = true;

            ProtocolCraft::ServerboundAcceptTeleportationPacket confirmPacket;
            confirmPacket.SetId_(posPacket.GetId_());

            ProtocolCraft::WriteContainer writeData;
            confirmPacket.Write(writeData);
            sendPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));

            ProtocolCraft::ServerboundMovePlayerPacketPosRot movePacket;
            movePacket.SetX(posPacket.GetX());
            movePacket.SetY(posPacket.GetY());
            movePacket.SetZ(posPacket.GetZ());
            movePacket.SetYRot(posPacket.GetYRot());
            movePacket.SetXRot(posPacket.GetXRot());
            movePacket.SetOnGround(true);

            ProtocolCraft::WriteContainer moveData;
            movePacket.Write(moveData);
            sendPacket(std::vector<uint8_t>(moveData.begin(), moveData.end()));

            m_engine->movementEnabled = true;
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x35:
#else
        case 0x38:
#endif
        { // Combat Kill (death message)
            ProtocolCraft::ClientboundPlayerCombatKillPacket killPacket;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            killPacket.Read(iter, len);
            m_engine->deathMessage = killPacket.GetMessage().GetText();
            if (m_engine->deathMessage.empty()) {
                m_engine->deathMessage = m_engine->parseChatComponent(killPacket.GetMessage().GetRawText());
            }
            LOGI("Death message: '%s'", m_engine->deathMessage.c_str());
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x51:
#else
        case 0x56:
#endif
        { // Set Experience
            ProtocolCraft::ClientboundSetExperiencePacket expPacket;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            expPacket.Read(iter, len);
            m_engine->experienceProgress = expPacket.GetExperienceProgress();
            m_engine->experienceLevel = expPacket.GetExperienceLevel();
            m_engine->totalExperience = expPacket.GetTotalExperience();
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x52:
#else
        case 0x57:
#endif
        { // Set Health
            ProtocolCraft::ClientboundSetHealthPacket healthPacket;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            healthPacket.Read(iter, len);
            m_engine->health = healthPacket.GetHealth();
            m_engine->food = healthPacket.GetFood();
            m_engine->foodSaturation = healthPacket.GetFoodSaturation();
            LOGI("Health: %.1f, Food: %d, Saturation: %.1f", m_engine->health, m_engine->food, m_engine->foodSaturation);
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x59:
#else
        case 0x5E:
#endif
        { // Set Time
            ProtocolCraft::ClientboundSetTimePacket timePacket;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            timePacket.Read(iter, len);
            Light::getInstance().setWorldDayTime(timePacket.GetDayTime());
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x48:
#else
        case 0x4D:
#endif
        { // Set Carried Item
            ProtocolCraft::ClientboundSetCarriedItemPacket carriedPacket;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            carriedPacket.Read(iter, len);
            int slot = (int)carriedPacket.GetSlot();
            if (slot >= 0 && slot <= 8) {
                m_engine->getInventory()->setSelectedSlot(slot);
            }
            break;
        }

        default:
            break;
    }
}
