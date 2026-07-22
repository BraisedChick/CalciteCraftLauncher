#include "NetworkManager/NetworkManager.h"
#include "ClientEngine/ClientEngine.h"
#include "utils.h"
#include "gui/GameUI.h"

#include "protocolCraft/Packets/Game/Clientbound/ClientboundChatPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSystemChatPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundPlayerChatPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundDisguisedChatPacket.hpp"

void NetworkManager::handleChat(int packetId, const std::vector<uint8_t>& data, size_t startPos) {
    switch (packetId) {
#if PROTOCOL_VERSION < 762
        case 0x0F:
#else
        case 0x63:
#endif
        { // System Chat
            try {
#if PROTOCOL_VERSION < 759
                ProtocolCraft::ClientboundChatPacket chatPacket;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                chatPacket.Read(iter, len);
                std::string rawJson = chatPacket.GetMessage().GetRawText();
                std::string text = rawJson.empty() ? chatPacket.GetMessage().GetText() : m_engine->parseChatComponent(rawJson);
                unsigned int chatColor = (rawJson.find("multiplayer.player.") != std::string::npos) ? 0xFF55FFFF : 0xFFFFFFFF;
                GameUI::getInstance().addChatMessage(text, chatColor);
#else
                ProtocolCraft::ClientboundSystemChatPacket sysChat;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                sysChat.Read(iter, len);
                std::string rawJson = sysChat.GetContent().GetRawText();
                std::string text = rawJson.empty() ? sysChat.GetContent().GetText() : m_engine->parseChatComponent(rawJson);
                unsigned int chatColor = (rawJson.find("multiplayer.player.") != std::string::npos) ? 0xFF55FFFF : 0xFFFFFFFF;
                GameUI::getInstance().addChatMessage(text, chatColor);
#endif
            } catch (const std::exception& e) {
                LOGW("Failed to parse chat packet: %s", e.what());
            }
            break;
        }

#if PROTOCOL_VERSION >= 762
        case 0x34:
        { // Player Chat
            try {
                ProtocolCraft::ClientboundPlayerChatPacket playerChat;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                playerChat.Read(iter, len);
                std::string text;
#if PROTOCOL_VERSION < 760
                text = playerChat.GetSignedContent().GetText();
                if (text.empty() && playerChat.GetUnsignedContent().has_value()) {
                    text = playerChat.GetUnsignedContent()->GetText();
                }
#else
#if PROTOCOL_VERSION < 761
                text = text.empty() && playerChat.GetMessage().GetUnsignedContent().has_value()
                    ? playerChat.GetMessage().GetUnsignedContent()->GetText()
                    : text;
#else
                if (playerChat.GetUnsignedContent().has_value()) {
                    std::string rawJson = playerChat.GetUnsignedContent()->GetRawText();
                    text = rawJson.empty() ? playerChat.GetUnsignedContent()->GetText() : m_engine->parseChatComponent(rawJson);
                }
#if PROTOCOL_VERSION >= 761
                if (text.empty()) {
                    text = playerChat.GetBody().GetContent();
                }
#endif
#endif
#endif
#if PROTOCOL_VERSION < 760
                std::string senderName = text.empty() ? "" : playerChat.GetSender().GetName().GetText();
#else
                std::string senderName = text.empty() ? "" : playerChat.GetChatType().GetName().GetText();
#endif
                if (!text.empty()) {
                    if (!senderName.empty()) {
                        text = "<" + senderName + "> " + text;
                    }
                    GameUI::getInstance().addChatMessage(text);
                }
            } catch (const std::exception& e) {
                LOGW("Failed to parse player chat: %s", e.what());
            }
            break;
        }

        case 0x1A:
        { // Disguised Chat
            try {
                ProtocolCraft::ClientboundDisguisedChatPacket disgChat;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                disgChat.Read(iter, len);
                std::string rawJson = disgChat.GetMessage().GetRawText();
                std::string text = rawJson.empty() ? disgChat.GetMessage().GetText() : m_engine->parseChatComponent(rawJson);
                if (!text.empty()) {
                    unsigned int chatColor = (rawJson.find("multiplayer.player.") != std::string::npos) ? 0xFF55FFFF : 0xFFFFFFFF;
                    GameUI::getInstance().addChatMessage(text, chatColor);
                }
            } catch (const std::exception& e) {
                LOGW("Failed to parse disguised chat: %s", e.what());
            }
            break;
        }
#endif

        default:
            break;
    }
}
