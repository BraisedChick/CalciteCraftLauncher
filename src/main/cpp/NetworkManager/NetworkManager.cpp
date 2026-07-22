#include "NetworkManager/NetworkManager.h"
#include "AESEncrypter.h"
#include "protocolCraft/BinaryReadWrite.hpp"
#include "Compression.h"
#include "utils.h"
#include "ClientEngine/ClientEngine.h"
#include "NetworkManager/handlers/PacketHandlerBase.h"
#include "ChunkManager.h"
#include "GLRenderer.h"
#include "Light.h"
#include "EntityManager.h"
#include "gui/GameUI.h"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundLevelChunkWithLightPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundClientInformationPacket.hpp"
#include "Chunk.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <errno.h>

NetworkManager::NetworkManager() : sock(-1), connected(false), encrypter(nullptr) {}
NetworkManager::~NetworkManager() { disconnect(); }

void NetworkManager::setEncrypter(AESEncrypter* enc) {
    encrypter = enc;
    if (enc) {
        LOGI("NetworkManager: AES encryption enabled");
    }
}

bool NetworkManager::isEncrypted() const {
    return encrypter != nullptr && encrypter->isInitialized();
}

bool NetworkManager::connect(const std::string& host, int port) {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        LOGE("Failed to create socket");
        return false;
    }
    struct hostent* server = gethostbyname(host.c_str());
    if (server == nullptr) {
        LOGE("Failed to resolve hostname");
        close(sock); sock = -1;
        return false;
    }
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);
    if (::connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        LOGE("Connect failed");
        close(sock); sock = -1;
        return false;
    }
    connected = true;
    return true;
}

void NetworkManager::disconnect() {
    if (sock != -1) close(sock);
    sock = -1;
    connected = false;
}

bool NetworkManager::sendRawPacket(const std::vector<uint8_t>& fullPacketData) {
    if (!connected || fullPacketData.empty()) return false;

    std::vector<uint8_t> packetData;

    if (Compression::isEnabled()) {
        int threshold = Compression::getThreshold();
        // fullPacketData 已经包含了 Packet ID
        if ((int)fullPacketData.size() >= threshold) {
            // 需要压缩
            std::vector<uint8_t> compressed = Compression::compress(fullPacketData);
            if (compressed.empty()) return false;

            // 压缩后的结构：[原始长度VarInt] [压缩数据]
            ProtocolCraft::WriteContainer originalLenBytes;
            ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(static_cast<int>(fullPacketData.size()), originalLenBytes);
            packetData.insert(packetData.end(), originalLenBytes.begin(), originalLenBytes.end());
            packetData.insert(packetData.end(), compressed.begin(), compressed.end());
        } else {
            // 未压缩，结构：[VarInt(0)] [Packet ID + payload]
            packetData.push_back(0); // VarInt(0)
            packetData.insert(packetData.end(), fullPacketData.begin(), fullPacketData.end());
        }
    } else {
        // 未启用压缩，直接使用 fullPacketData（已包含 Packet ID）
        packetData = fullPacketData;
    }

    // 添加包长度前缀
    ProtocolCraft::WriteContainer lenBytes;
    ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(static_cast<int>(packetData.size()), lenBytes);
    std::vector<uint8_t> finalPacket;
    finalPacket.insert(finalPacket.end(), lenBytes.begin(), lenBytes.end());
    finalPacket.insert(finalPacket.end(), packetData.begin(), packetData.end());

    // 如果启用了 AES 加密，加密整个数据 — 参考 Botcraft TCP_Com::SendPacket
    if (isEncrypted()) {
        finalPacket = encrypter->Encrypt(finalPacket);
    }

    int sent = send(sock, finalPacket.data(), finalPacket.size(), 0);
    return sent == (int)finalPacket.size();
}

std::vector<uint8_t> NetworkManager::receivePacket() {
    if (!connected) return {};

    // 如果启用了 AES 加密，需要先逐字节读取并解密 — 参考 Botcraft TCP_Com
    // 因为加密后包长度 VarInt 也被加密了，需要边读边解密
    if (isEncrypted()) {
        return receiveEncryptedPacket();
    }

    // 读取包长度
    std::vector<uint8_t> lenBuf(5);
    int bytesRead = 0;
    int packetLen = -1;
    while (bytesRead < 5) {
        int n = recv(sock, lenBuf.data() + bytesRead, 1, 0);
        if (n <= 0) return {};
        bytesRead++;
        
        // 尝试解码 VarInt
        ProtocolCraft::ReadIterator iter = lenBuf.begin();
        size_t length = bytesRead;
        try {
            packetLen = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, length);
            break;
        } catch (...) {
            // VarInt 不完整，继续读取
            continue;
        }
    }
    if (packetLen == -1) return {};

    // 读取数据块
    std::vector<uint8_t> rawData(packetLen);
    int total = 0;
    while (total < packetLen) {
        int n = recv(sock, rawData.data() + total, packetLen - total, 0);
        if (n <= 0) return {};
        total += n;
    }

    // 如果启用了接收压缩，处理压缩头
    if (Compression::isReceiveEnabled()) {
        ProtocolCraft::ReadIterator iter = rawData.begin();
        size_t length = rawData.size();
        try {
            int uncompressedLen = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, length);
            // 计算剩余数据的起始位置
            size_t remainingStart = rawData.size() - length;
            std::vector<uint8_t> rest(rawData.begin() + remainingStart, rawData.end());
            if (uncompressedLen == 0) {
                // 数据未压缩
                return rest;
            } else {
                // 需要解压
                return Compression::decompress(rest, uncompressedLen);
            }
        } catch (...) {
            return {};
        }
    } else {
        return rawData;
    }
}

// ===== Handler 注册表与分发 =====

void NetworkManager::registerHandlers() {
    auto reg = [this](int pid, PacketHandler handler) {
        m_packetHandlers[pid] = handler;
    };

#if PROTOCOL_VERSION < 762
    // 1.18.2
    reg(0x26, &NetworkManager::handleLogin);
    reg(0x21, &NetworkManager::handlePlayerStatus);
    reg(0x38, &NetworkManager::handlePlayerStatus);
    reg(0x22, &NetworkManager::handleWorld);
    reg(0x0C, &NetworkManager::handleWorld);
    reg(0x25, &NetworkManager::handleWorld);
    reg(0x3F, &NetworkManager::handleWorld);
    reg(0x14, &NetworkManager::handleInventory);
    reg(0x16, &NetworkManager::handleInventory);
    reg(0x35, &NetworkManager::handlePlayerStatus);
    reg(0x51, &NetworkManager::handlePlayerStatus);
    reg(0x52, &NetworkManager::handlePlayerStatus);
    reg(0x1E, &NetworkManager::handleLogin);
    reg(0x3D, &NetworkManager::handleLogin);
    reg(0x59, &NetworkManager::handlePlayerStatus);
    reg(0x48, &NetworkManager::handlePlayerStatus);
    reg(0x00, &NetworkManager::handleEntity);
    reg(0x02, &NetworkManager::handleEntity);
    reg(0x04, &NetworkManager::handleEntity);
    reg(0x2A, &NetworkManager::handleEntity);
    reg(0x29, &NetworkManager::handleEntity);
    reg(0x2B, &NetworkManager::handleEntity);
    reg(0x62, &NetworkManager::handleEntity);
    reg(0x3A, &NetworkManager::handleEntity);
    reg(0x4F, &NetworkManager::handleEntity);
    reg(0x2E, &NetworkManager::handleInventory);
    reg(0x13, &NetworkManager::handleInventory);
    reg(0x0F, &NetworkManager::handleChat);
#else
    // 1.19+
    reg(0x28, &NetworkManager::handleLogin);
    reg(0x23, &NetworkManager::handlePlayerStatus);
    reg(0x3C, &NetworkManager::handlePlayerStatus);
    reg(0x24, &NetworkManager::handleWorld);
    reg(0x0A, &NetworkManager::handleWorld);
    reg(0x27, &NetworkManager::handleWorld);
    reg(0x43, &NetworkManager::handleWorld);
    reg(0x12, &NetworkManager::handleInventory);
    reg(0x14, &NetworkManager::handleInventory);
    reg(0x38, &NetworkManager::handlePlayerStatus);
    reg(0x56, &NetworkManager::handlePlayerStatus);
    reg(0x57, &NetworkManager::handlePlayerStatus);
    reg(0x1F, &NetworkManager::handleLogin);
    reg(0x41, &NetworkManager::handleLogin);
    reg(0x5E, &NetworkManager::handlePlayerStatus);
    reg(0x4D, &NetworkManager::handlePlayerStatus);
    reg(0x00, &NetworkManager::handlePlayerStatus); // BundlePacket
    reg(0x01, &NetworkManager::handleEntity);
#if PROTOCOL_VERSION < 764
    reg(0x03, &NetworkManager::handleEntity); // AddPlayer
#endif
    reg(0x2C, &NetworkManager::handleEntity);
    reg(0x2B, &NetworkManager::handleEntity);
    reg(0x2D, &NetworkManager::handleEntity);
    reg(0x68, &NetworkManager::handleEntity);
    reg(0x3E, &NetworkManager::handleEntity);
    reg(0x54, &NetworkManager::handleEntity);
    reg(0x30, &NetworkManager::handleInventory);
    reg(0x11, &NetworkManager::handleInventory);
    reg(0x63, &NetworkManager::handleChat);
    reg(0x34, &NetworkManager::handleChat);
    reg(0x1A, &NetworkManager::handleChat);
#endif
}

void NetworkManager::handlePlayPacket(int packetId,
                                      const std::vector<uint8_t>& data, size_t startPos) {
    if (m_packetHandlers.empty()) {
        registerHandlers();
    }
    auto it = m_packetHandlers.find(packetId);
    if (it != m_packetHandlers.end()) {
        try {
            (this->*(it->second))(packetId, data, startPos);
        } catch (const std::exception& e) {
            LOGE("Error handling packet 0x%02X: %s", packetId, e.what());
        }
    }
}

// ===== 数据包处理线程 =====

void NetworkManager::urgentProcessorFunc() {
    while (urgentProcessorRunning) {
        PacketTask task;
        {
            std::unique_lock<std::mutex> lock(urgentQueueMutex);
            urgentCV.wait(lock, [this]() {
                return !urgentQueue.empty() || !urgentProcessorRunning;
            });
            if (!urgentProcessorRunning) break;
            task = std::move(urgentQueue.front());
            urgentQueue.pop();
        }
        handlePlayPacket(task.packetId, task.data, task.startPos);
    }
}

void NetworkManager::normalProcessorFunc() {
    while (normalProcessorRunning) {
        PacketTask task;
        {
            std::unique_lock<std::mutex> lock(normalQueueMutex);
            normalCV.wait(lock, [this]() {
                return !normalQueue.empty() || !normalProcessorRunning;
            });
            if (!normalProcessorRunning) break;
            task = std::move(normalQueue.front());
            normalQueue.pop();
        }
        handlePlayPacket(task.packetId, task.data, task.startPos);
    }
}

void NetworkManager::chunkWorkerFunc() {
    while (chunkWorkerRunning) {
        ChunkLoadTask task;
        {
            std::unique_lock<std::mutex> lock(chunkQueueMutex);
            chunkCV.wait(lock, [this]() {
                return !chunkQueue.empty() || !chunkWorkerRunning;
            });
            if (!chunkWorkerRunning) break;
            task = std::move(chunkQueue.front());
            chunkQueue.pop();
        }
        if (m_engine && m_engine->chunkManager) {
            parseChunkDataPacket(task.rawData, 0);
        }
    }
}

void NetworkManager::parseChunkDataPacket(const std::vector<uint8_t>& data, size_t startPos) {
    if (!m_engine || !m_engine->chunkManager) return;

    ProtocolCraft::ClientboundLevelChunkWithLightPacket chunkPacket;
    std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
    auto iter = pktData.cbegin();
    size_t len = pktData.size();
    try {
        chunkPacket.Read(iter, len);
    } catch (const std::exception& e) {
        LOGW("Chunk parse error: %s", e.what());
        return;
    }

    int cx = chunkPacket.GetX();
    int cz = chunkPacket.GetZ();

    // 使用 loadChunk 加载区块（与原始 chunkWorkerFunc 保持一致）
    const auto& buffer_data = chunkPacket.GetChunkData().GetBuffer();
    if (buffer_data.empty()) return;

    std::vector<uint8_t> rawData(buffer_data.begin(), buffer_data.end());
    std::vector<uint8_t> emptyHeightmaps;
    std::vector<uint8_t> emptyBlockEntities;

    // 从原始数据读取 bitMask（位置 8 起 8 字节）
    auto rawIter = data.cbegin() + startPos + 8;
    long long bitMask = 0;
    for (int i = 0; i < 8; i++) {
        bitMask = (bitMask << 8) | (unsigned char)*rawIter;
        ++rawIter;
    }

    try {
        m_engine->chunkManager->loadChunk(cx, cz, rawData, true, bitMask,
                                          emptyHeightmaps, emptyBlockEntities,
                                          m_engine->dimensionMinY);

        // 提取光照数据
        auto chunk = m_engine->chunkManager->getChunk(cx, cz);
        if (chunk) {
            const auto& lightData = chunkPacket.GetLightData();
            const auto& skyMasks = lightData.GetSkyYMask();
            const auto& blockMasks = lightData.GetBlockYMask();
            const auto& emptySkyMasks = lightData.GetEmptySkyYMask();
            const auto& emptyBlockMasks = lightData.GetEmptyBlockYMask();
            const auto& skyUpdates = lightData.GetSkyUpdates();
            const auto& blockUpdates = lightData.GetBlockUpdates();

            uint64_t skyMask = skyMasks.empty() ? 0 : skyMasks[0];
            uint64_t blockMask = blockMasks.empty() ? 0 : blockMasks[0];
            uint64_t emptySkyMask = emptySkyMasks.empty() ? 0 : emptySkyMasks[0];
            uint64_t emptyBlockMask = emptyBlockMasks.empty() ? 0 : emptyBlockMasks[0];

            int skyIdx = 0, blockIdx = 0;
            int sectionCount = (int)chunk->sections.size();
            for (int i = 0; i < sectionCount; i++) {
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
        }

        if (m_engine->glRenderer) {
            m_engine->glRenderer->markChunkForUpdate(cx, cz);
        }
    } catch (const std::exception& e) {
        LOGW("Chunk worker: failed to load chunk (%d,%d): %s", cx, cz, e.what());
    }
}

size_t NetworkManager::calculateNBTSize(const std::vector<uint8_t>& data, size_t startPos) {
    return 0;
}

bool NetworkManager::sendPacket(const std::vector<uint8_t>& data) {
    return sendRawPacket(data);
}

bool NetworkManager::isCompressionEnabled() const {
    return Compression::isEnabled();
}

void NetworkManager::startPlayLoop() {
    // 启动处理线程
    urgentProcessorRunning = true;
    urgentProcessor = std::thread(&NetworkManager::urgentProcessorFunc, this);
    normalProcessorRunning = true;
    normalProcessor = std::thread(&NetworkManager::normalProcessorFunc, this);
    chunkWorkerRunning = true;
    chunkWorker = std::thread(&NetworkManager::chunkWorkerFunc, this);

    bool clientInfoSent = false;

    // ========== PLAY 状态主循环 ==========
    while (true) {
        auto resp = receivePacket();
        if (resp.empty()) {
            LOGI("Connection closed");
            break;
        }

        size_t pos = 0;
        ProtocolCraft::ReadIterator iter = resp.cbegin();
        size_t remaining = resp.size();
        int pid = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, remaining);
        pos = resp.size() - remaining;

        // 收到第一个 PLAY 状态包后，发送 ClientInformation
        if (!clientInfoSent) {
            clientInfoSent = true;

            ProtocolCraft::ServerboundClientInformationPacket infoPacket;
            infoPacket.SetLanguage("en_US");
            infoPacket.SetViewDistance(10);
            infoPacket.SetChatVisibility(0);
            infoPacket.SetChatColors(true);
            infoPacket.SetModelCustomisation(0x7F);
            infoPacket.SetMainHand(1);
            infoPacket.SetTextFilteringEnabled(false);
            infoPacket.SetAllowListing(true);

            ProtocolCraft::WriteContainer writeData;
            infoPacket.Write(writeData);
            sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
            LOGI("Sent Client Information (ViewDistance=10)");
        }

        // 紧急包（延迟敏感）：位置、方块更新、生命、保活、游戏事件
#if PROTOCOL_VERSION >= 762
        if (pid == 0x3C || pid == 0x0A || pid == 0x43 || pid == 0x57 ||
            pid == 0x23 || pid == 0x1F || pid == 0x41) {
#else
        if (pid == 0x38 || pid == 0x0C || pid == 0x3F || pid == 0x52 ||
            pid == 0x21 || pid == 0x1E || pid == 0x3D) {
#endif
            std::lock_guard<std::mutex> lock(urgentQueueMutex);
            urgentQueue.push({pid, std::move(resp), pos});
            urgentCV.notify_one();
        } else {
            std::lock_guard<std::mutex> lock(normalQueueMutex);
            normalQueue.push({pid, std::move(resp), pos});
            normalCV.notify_one();
        }
    }

    // 停止处理线程
    urgentProcessorRunning = false;
    urgentCV.notify_all();
    if (urgentProcessor.joinable()) {
        urgentProcessor.join();
    }

    normalProcessorRunning = false;
    normalCV.notify_all();
    if (normalProcessor.joinable()) {
        normalProcessor.join();
    }

    chunkWorkerRunning = false;
    chunkCV.notify_all();
    if (chunkWorker.joinable()) {
        chunkWorker.join();
    }
}

bool NetworkManager::isConnected() const { return connected; }

void NetworkManager::enqueueChunkData(std::vector<uint8_t> rawData) {
    std::lock_guard<std::mutex> lock(chunkQueueMutex);
    chunkQueue.push({std::move(rawData)});
    chunkCV.notify_one();
}

/**
 * 加密模式下的数据包接收
 * 参考 Botcraft TCP_Com::RecvPacket 的方式：逐字节读取 → 解密 → 拼接
 * AES CFB8 是流加密，可以逐字节处理
 */
std::vector<uint8_t> NetworkManager::receiveEncryptedPacket() {
    // 逐字节读取并解密，直到能解析出 VarInt 包长度
    std::vector<uint8_t> decryptedBuf;
    int packetLen = -1;

    while (true) {
        uint8_t encryptedByte;
        int n = recv(sock, &encryptedByte, 1, 0);
        if (n <= 0) return {};

        // 解密单字节
        std::vector<uint8_t> encBuf = {encryptedByte};
        std::vector<uint8_t> decByte = encrypter->Decrypt(encBuf);
        if (!decByte.empty()) {
            decryptedBuf.push_back(decByte[0]);
        }

        // 尝试解码 VarInt
        ProtocolCraft::ReadIterator iter = decryptedBuf.begin();
        size_t length = decryptedBuf.size();
        try {
            packetLen = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, length);
            break;
        } catch (...) {
            // VarInt 不完整，继续读取
            if (decryptedBuf.size() >= 5) return {}; // VarInt 最大5字节
            continue;
        }
    }
    if (packetLen <= 0) return {};

    // 计算已读取的数据长度（VarInt 占了多少字节）
    size_t varIntSize = decryptedBuf.size();

    // 读取剩余数据
    int remaining = packetLen - (varIntSize > 0 ? 0 : 0);
    // 注意：VarInt 长度不包含在 packetLen 中，packetLen 就是数据长度
    // 但我们可能已经在 VarInt 后面多读了一些字节（不太可能，因为是一字节一字节读的）

    // 继续读取 packetLen 字节的加密数据
    std::vector<uint8_t> encryptedData(packetLen);
    int total = 0;
    while (total < packetLen) {
        int n = recv(sock, encryptedData.data() + total, packetLen - total, 0);
        if (n <= 0) return {};
        total += n;
    }

    // 解密数据
    std::vector<uint8_t> rawData = encrypter->Decrypt(encryptedData);
    if (rawData.size() < (size_t)packetLen) {
        // AES CFB8 解密后大小应该和输入一致
        LOGW("Decrypted size mismatch: got %zu, expected %d", rawData.size(), packetLen);
    }

    // 处理压缩头（与 receivePacket 中的逻辑一致）
    if (Compression::isReceiveEnabled()) {
        ProtocolCraft::ReadIterator iter = rawData.begin();
        size_t length = rawData.size();
        try {
            int uncompressedLen = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, length);
            size_t remainingStart = rawData.size() - length;
            std::vector<uint8_t> rest(rawData.begin() + remainingStart, rawData.end());
            if (uncompressedLen == 0) {
                // 数据未压缩
                return rest;
            } else {
                // 需要解压
                return Compression::decompress(rest, uncompressedLen);
            }
        } catch (...) {
            return {};
        }
    } else {
        return rawData;
    }
}