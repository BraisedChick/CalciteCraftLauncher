#include "NetworkManager/NetworkManager.h"
#include "ClientEngine/GameEngine.h"
#include "utils.h"
#include "ChunkManager.h"
#include "Light.h"
#include "GLRenderer.h"

#include "protocolCraft/Packets/Game/Clientbound/ClientboundLevelChunkWithLightPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundBlockUpdatePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundLightUpdatePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSectionBlocksUpdatePacket.hpp"

void NetworkManager::handleWorld(int packetId, const std::vector<uint8_t>& data, size_t startPos) {
    switch (packetId) {
#if PROTOCOL_VERSION < 762
        case 0x22:
#else
        case 0x24:
#endif
        { // Chunk Data
            {
                std::lock_guard<std::mutex> lock(m_engine->netMutex);
                enqueueChunkData(std::vector<uint8_t>(data.begin() + startPos, data.end()));
            }
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x0C:
#else
        case 0x0A:
#endif
        { // Block Update
            ProtocolCraft::ClientboundBlockUpdatePacket blockPacket;
            std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();
            blockPacket.Read(iter, length);

            auto pos = blockPacket.GetPos();
            int blockX = pos.GetX();
            int blockY = pos.GetY();
            int blockZ = pos.GetZ();
            int blockState = blockPacket.GetBlockstate();

            int chunkX = blockX >> 4;
            int chunkZ = blockZ >> 4;
            int localX = blockX & 15;
            int localZ = blockZ & 15;

            if (m_engine->chunkManager) {
                auto chunk = m_engine->chunkManager->getChunk(chunkX, chunkZ);
                if (chunk) {
                    chunk->setBlockState(localX, blockY, localZ, blockState);
                    Light::getInstance().queueBlockLightRecalc(blockX, blockY, blockZ);
                    if (m_engine->getRenderer()) {
                        m_engine->getRenderer()->markChunkForUpdate(chunkX, chunkZ);
                    }
                } else {
                    LOGW("BlockUpdate: chunk (%d, %d) not loaded", chunkX, chunkZ);
                }
            }
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x25:
#else
        case 0x27:
#endif
        { // Light Update
            try {
                ProtocolCraft::ClientboundLightUpdatePacket lightPacket;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto subIter = pktData.cbegin();
                size_t subLen = pktData.size();
                lightPacket.Read(subIter, subLen);
                int lx = lightPacket.GetX();
                int lz = lightPacket.GetZ();
                if (m_engine->chunkManager) {
                    auto chunk = m_engine->chunkManager->getChunk(lx, lz);
                    if (chunk) {
                        const auto& ld = lightPacket.GetLightData();
                        const auto& skyMasks = ld.GetSkyYMask();
                        const auto& blockMasks = ld.GetBlockYMask();
                        const auto& emptySkyMasks = ld.GetEmptySkyYMask();
                        const auto& emptyBlockMasks = ld.GetEmptyBlockYMask();
                        const auto& skyUpdates = ld.GetSkyUpdates();
                        const auto& blockUpdates = ld.GetBlockUpdates();
                        uint64_t skyMask = skyMasks.empty() ? 0 : skyMasks[0];
                        uint64_t blockMask = blockMasks.empty() ? 0 : blockMasks[0];
                        uint64_t emptySkyMask = emptySkyMasks.empty() ? 0 : emptySkyMasks[0];
                        uint64_t emptyBlockMask = emptyBlockMasks.empty() ? 0 : emptyBlockMasks[0];
                        int skyIdx = 0, blockIdx = 0;
                        for (int i = 0; i < (int)chunk->sections.size(); i++) {
                            auto& sec = chunk->sections[i];
                            if (!sec) continue;
                            int lightBit = i + 1;
                            if (skyMask & (1ULL << lightBit)) {
                                if (skyIdx < (int)skyUpdates.size()) {
                                    const auto& d = skyUpdates[skyIdx];
                                    sec->skyLight.resize(2048);
                                    memcpy(sec->skyLight.data(), d.data(), std::min(d.size(), (size_t)2048));
                                }
                                skyIdx++;
                            } else if (emptySkyMask & (1ULL << lightBit)) {
                                sec->skyLight.assign(2048, 0);
                            }
                            if (blockMask & (1ULL << lightBit)) {
                                if (blockIdx < (int)blockUpdates.size()) {
                                    const auto& d = blockUpdates[blockIdx];
                                    sec->blockLight.resize(2048);
                                    memcpy(sec->blockLight.data(), d.data(), std::min(d.size(), (size_t)2048));
                                }
                                blockIdx++;
                            } else if (emptyBlockMask & (1ULL << lightBit)) {
                                sec->blockLight.assign(2048, 0);
                            }
                        }
                        if (m_engine->getRenderer()) m_engine->getRenderer()->markChunkForUpdate(lx, lz);
                    }
                }
            } catch (const std::exception& e) {
                LOGW("LightUpdate: parse error: %s", e.what());
            }
            break;
        }

#if PROTOCOL_VERSION < 762
        case 0x3F:
#else
        case 0x43:
#endif
        { // Section Blocks Update
            ProtocolCraft::ClientboundSectionBlocksUpdatePacket sectionPacket;
            std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();
            sectionPacket.Read(iter, length);

            long long sectionPos = sectionPacket.GetSectionPos();
            uint64_t rawPos = (uint64_t)sectionPos;
            int chunkX = (int)((rawPos >> 42) & 0x3FFFFF);
            if (chunkX >= 2097152) chunkX -= 4194304;
            int chunkZ = (int)((rawPos >> 20) & 0x3FFFFF);
            if (chunkZ >= 2097152) chunkZ -= 4194304;
            int sectionY = (int)(rawPos & 0xFFFFF);
            if (sectionY >= 524288) sectionY -= 1048576;

            if (!m_engine->chunkManager) break;
            auto chunk = m_engine->chunkManager->getChunk(chunkX, chunkZ);
            if (!chunk) {
                LOGE("SectionBlocksUpdate: chunk (%d, %d) not loaded", chunkX, chunkZ);
                break;
            }

            const auto& posState = sectionPacket.GetPosState();
            for (const auto& entry : posState) {
                uint64_t entryVal = (uint64_t)entry;
                int sectionLocalIndex = (int)(entryVal & 0xFFF);
                int blockState = (int)(entryVal >> 12);
                int localX = (sectionLocalIndex >> 8) & 0xF;
                int localZ = (sectionLocalIndex >> 4) & 0xF;
                int localY = sectionLocalIndex & 0xF;
                int blockY = sectionY * 16 + localY;
                chunk->setBlockState(localX, blockY, localZ, blockState);
            }

            if (m_engine->getRenderer()) {
                m_engine->getRenderer()->markChunkForUpdate(chunkX, chunkZ);
            }
            break;
        }

        default:
            break;
    }
}
