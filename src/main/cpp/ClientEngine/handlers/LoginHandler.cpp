#include "../ClientEngine.h"
#include "utils.h"
#include "EntityManager.h"
#include "Entity.h"
#include "Collision.h"
#include "CameraController.h"
#include "Light.h"
#include "PlayerInventory.h"
#include "MinecraftVersion.h"
#include "BiomeColorManager.h"
#include "ChunkManager.h"
#include "GLRenderer.h"

#include "protocolCraft/Packets/Game/Clientbound/ClientboundLoginPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundGameEventPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundRespawnPacket.hpp"

void ClientEngine::handleLogin(int packetId, const std::vector<uint8_t>& data, size_t startPos) {
    switch (packetId) {
#if PROTOCOL_VERSION < 762
        case 0x26:
#else
        case 0x28:
#endif
        { // Login (Play) - 玩家初始状态
            LOGI("Received ClientboundLoginPacket (Play)");

            ProtocolCraft::ClientboundLoginPacket loginPacket;
            std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();

            try {
                loginPacket.Read(iter, length);
                gameMode = loginPacket.GetGameType();
                Collision::getInstance().setGameMode(gameMode);
                int playerId = loginPacket.GetPlayerId();
                this->playerId = playerId;
                LOGI("Player ID: %d, GameType: %d", playerId, gameMode);

                {
                    Entity playerEntity;
                    playerEntity.entityId = playerId;
                    playerEntity.type = EntityType::PLAYER;
                    EntityManager::getInstance().addEntity(playerEntity);
                }
                Collision::getInstance().setPlayerEntityId(playerId);

                // 从 RegistryHolder 解析服务器端 biome 注册表
                try {
                    const auto& registryHolder = loginPacket.GetRegistryHolder();
                    if (registryHolder.contains("minecraft:worldgen/biome")) {
                        const auto& biomeRegistry = registryHolder["minecraft:worldgen/biome"];
                        if (biomeRegistry.is<ProtocolCraft::NBT::TagCompound>() &&
                            biomeRegistry.contains("value")) {
                            const auto& value = biomeRegistry["value"];
                            if (value.is_list_of<ProtocolCraft::NBT::TagCompound>()) {
                                const auto& entries = value.as_list_of<ProtocolCraft::NBT::TagCompound>();
                                std::map<std::string, BiomeColorManager::BiomeEntry> serverBiomes;
                                std::map<int32_t, std::string> serverIdToName;
                                for (const auto& entry : entries) {
                                    auto nameIt = entry.find("name");
                                    auto idIt = entry.find("id");
                                    auto elementIt = entry.find("element");
                                    if (nameIt == entry.end() || elementIt == entry.end()) continue;

                                    std::string biomeName = nameIt->second.get<std::string>();
                                    const auto& element = elementIt->second;

                                    if (idIt != entry.end() && idIt->second.is<int>()) {
                                        serverIdToName[idIt->second.get<int>()] = biomeName;
                                    }

                                    BiomeColorManager::BiomeEntry biomeEntry;

                                    if (element.is<ProtocolCraft::NBT::TagCompound>()) {
                                        const auto& elemCompound = element.get<ProtocolCraft::NBT::TagCompound>();
                                        auto tempIt = elemCompound.find("temperature");
                                        auto downIt = elemCompound.find("downfall");
                                        if (tempIt != elemCompound.end()) {
                                            if (tempIt->second.is<ProtocolCraft::NBT::TagDouble>())
                                                biomeEntry.temperature = tempIt->second.get<double>();
                                            else if (tempIt->second.is<ProtocolCraft::NBT::TagFloat>())
                                                biomeEntry.temperature = tempIt->second.get<float>();
                                        }
                                        if (downIt != elemCompound.end()) {
                                            if (downIt->second.is<ProtocolCraft::NBT::TagDouble>())
                                                biomeEntry.downfall = downIt->second.get<double>();
                                            else if (downIt->second.is<ProtocolCraft::NBT::TagFloat>())
                                                biomeEntry.downfall = downIt->second.get<float>();
                                        }

                                        auto effectsIt = elemCompound.find("effects");
                                        if (effectsIt != elemCompound.end() && effectsIt->second.is<ProtocolCraft::NBT::TagCompound>()) {
                                            const auto& effects = effectsIt->second.get<ProtocolCraft::NBT::TagCompound>();
                                            auto grassColorIt = effects.find("grass_color");
                                            if (grassColorIt != effects.end() && grassColorIt->second.is<int>()) {
                                                int color = grassColorIt->second.get<int>();
                                                biomeEntry.hasFixedGrassColor = true;
                                                biomeEntry.fixedGrassR = (color >> 16) & 0xFF;
                                                biomeEntry.fixedGrassG = (color >> 8) & 0xFF;
                                                biomeEntry.fixedGrassB = color & 0xFF;
                                            }
                                            auto foliageColorIt = effects.find("foliage_color");
                                            if (foliageColorIt != effects.end() && foliageColorIt->second.is<int>()) {
                                                int color = foliageColorIt->second.get<int>();
                                                biomeEntry.hasFixedFoliageColor = true;
                                                biomeEntry.fixedFoliageR = (color >> 16) & 0xFF;
                                                biomeEntry.fixedFoliageG = (color >> 8) & 0xFF;
                                                biomeEntry.fixedFoliageB = color & 0xFF;
                                            }
                                            auto waterColorIt = effects.find("water_color");
                                            if (waterColorIt != effects.end() && waterColorIt->second.is<int>()) {
                                                int color = waterColorIt->second.get<int>();
                                                biomeEntry.hasFixedWaterColor = true;
                                                biomeEntry.fixedWaterR = (color >> 16) & 0xFF;
                                                biomeEntry.fixedWaterG = (color >> 8) & 0xFF;
                                                biomeEntry.fixedWaterB = color & 0xFF;
                                            }
                                            auto modifierIt = effects.find("grass_color_modifier");
                                            if (modifierIt != effects.end() && modifierIt->second.is<ProtocolCraft::NBT::TagString>()) {
                                                std::string modifier = modifierIt->second.get<std::string>();
                                                if (modifier == "swamp") {
                                                    biomeEntry.hasFixedGrassColor = true;
                                                    biomeEntry.fixedGrassR = 106;
                                                    biomeEntry.fixedGrassG = 112;
                                                    biomeEntry.fixedGrassB = 57;
                                                } else if (modifier == "dark_forest") {
                                                    biomeEntry.hasFixedGrassColor = true;
                                                    biomeEntry.fixedGrassR = 64;
                                                    biomeEntry.fixedGrassG = 128;
                                                    biomeEntry.fixedGrassB = 64;
                                                }
                                            }
                                        }
                                    }
                                    serverBiomes[biomeName] = biomeEntry;
                                }
                                if (!serverBiomes.empty()) {
                                    LOGI("Applying server biome registry mapping, %zu entries", serverBiomes.size());
                                    BiomeColorManager::getInstance().applyServerBiomeMapping(serverBiomes);
                                    if (!serverIdToName.empty()) {
                                        BiomeColorManager::getInstance().setServerIdMapping(serverIdToName);
                                    }
                                }
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    LOGW("Failed to parse RegistryHolder biomes: %s", e.what());
                }

                // 从 Login 包的 DimensionType 解析世界高度参数
#if PROTOCOL_VERSION < 759
                try {
                    const auto& dimType = loginPacket.GetDimensionType();
                    if (dimType.contains("min_y") && dimType.contains("height")) {
                        dimensionMinY = dimType["min_y"].get<int>();
                        dimensionHeight = dimType["height"].get<int>();
                        VersionManager::getInstance().setDimensionConfig(dimensionMinY, dimensionMinY + dimensionHeight);
                    }
                } catch (const std::exception& e) {
                    LOGW("Failed to parse DimensionType from Login: %s", e.what());
                    dimensionMinY = -64;
                    dimensionHeight = 384;
                    VersionManager::getInstance().setDimensionConfig(-64, 320);
                }
#else
                try {
                    std::string dimTypeName = loginPacket.GetDimensionType().GetFull();
                    const auto& registryHolder = loginPacket.GetRegistryHolder();
                    if (registryHolder.contains("minecraft:dimension_type") &&
                        registryHolder["minecraft:dimension_type"].contains("value")) {
                        const auto& value = registryHolder["minecraft:dimension_type"]["value"];
                        if (value.is_list_of<ProtocolCraft::NBT::TagCompound>()) {
                            const auto& entries = value.as_list_of<ProtocolCraft::NBT::TagCompound>();
                            for (const auto& entry : entries) {
                                auto nameIt = entry.find("name");
                                auto elementIt = entry.find("element");
                                if (nameIt == entry.end() || elementIt == entry.end()) continue;
                                std::string name = nameIt->second.get<std::string>();
                                if (name == dimTypeName && elementIt->second.is<ProtocolCraft::NBT::TagCompound>()) {
                                    const auto& elem = elementIt->second.get<ProtocolCraft::NBT::TagCompound>();
                                    auto minYIt = elem.find("min_y");
                                    auto heightIt = elem.find("height");
                                    if (minYIt != elem.end() && heightIt != elem.end()) {
                                        dimensionMinY = minYIt->second.get<int>();
                                        dimensionHeight = heightIt->second.get<int>();
                                        VersionManager::getInstance().setDimensionConfig(dimensionMinY, dimensionMinY + dimensionHeight);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                } catch (const std::exception& e) {
                    LOGW("Failed to parse dimension from RegistryHolder: %s", e.what());
                }
#endif
                LOGI("Dimension info: min_y=%d, height=%d (Y range: %d to %d)",
                     dimensionMinY, dimensionHeight, dimensionMinY, dimensionMinY + dimensionHeight - 1);
            } catch (const std::exception& e) {
                LOGE("Failed to parse Login packet: %s", e.what());
            }
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x1E:
#else
        case 0x1F:
#endif
        { // Game Event
            ProtocolCraft::ClientboundGameEventPacket gameEventPacket;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            gameEventPacket.Read(iter, len);
            if (gameEventPacket.GetType() == 3) {
                int newMode = static_cast<int>(gameEventPacket.GetParam());
                gameMode = newMode;
                Collision::getInstance().setGameMode(newMode);
            }
            LOGI("gamemode change");
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x3D:
#else
        case 0x41:
#endif
        { // Respawn
            ProtocolCraft::ClientboundRespawnPacket respawnPacket;
            std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
            auto iter = pktData.cbegin();
            size_t len = pktData.size();
            respawnPacket.Read(iter, len);
            int newMode = respawnPacket.GetPlayerGameType();
            gameMode = newMode;
            Collision::getInstance().setGameMode(newMode);

#if PROTOCOL_VERSION < 759
            try {
                const auto& dimType = respawnPacket.GetDimensionType();
                if (dimType.contains("min_y") && dimType.contains("height")) {
                    dimensionMinY = dimType["min_y"].get<int>();
                    dimensionHeight = dimType["height"].get<int>();
                    VersionManager::getInstance().setDimensionConfig(dimensionMinY, dimensionMinY + dimensionHeight);
                    LOGI("Respawn: New dimension min_y=%d, height=%d", dimensionMinY, dimensionHeight);
                }
            } catch (const std::exception& e) {
                LOGW("Failed to parse DimensionType from Respawn: %s", e.what());
            }
#else
            LOGI("Respawn: dimension changed to %s",
                 respawnPacket.GetDimensionType().GetFull().c_str());
#endif

            LOGI("Respawn: Dimension change, clearing chunks and entities");
            if (chunkManager) {
                chunkManager->clear();
            }
            if (glRenderer) {
                glRenderer->clearChunks();
            }
            EntityManager::getInstance().removeAllEntities();
            break;
        }

        default:
            break;
    }
}
