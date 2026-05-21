#include "NetworkManager.h"
#include "protocolCraft/BinaryReadWrite.hpp"
#include "Compression.h"
#include "utils.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>
#include <errno.h>

NetworkManager::NetworkManager() : sock(-1), connected(false) {}
NetworkManager::~NetworkManager() { disconnect(); }

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

bool NetworkManager::sendPacket(int packetId, const std::vector<uint8_t>& payload) {
    if (!connected) return false;

    std::vector<uint8_t> packetData;

    if (Compression::isEnabled()) {
        int threshold = Compression::getThreshold();
        LOGI("Sending packet %d, payload size=%d, compression enabled, threshold=%d",
             packetId, static_cast<int>(payload.size()), threshold);

        // 构建未压缩的包内容：包ID + payload
        ProtocolCraft::WriteContainer idBytes;
        ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(packetId, idBytes);
        packetData.insert(packetData.end(), idBytes.begin(), idBytes.end());
        packetData.insert(packetData.end(), payload.begin(), payload.end());

        if ((int)packetData.size() >= threshold) {
            // 需要压缩
            LOGI("Compressing packet %d, uncompressed size=%d", packetId, static_cast<int>(packetData.size()));
            std::vector<uint8_t> compressed = Compression::compress(packetData);
            if (compressed.empty()) return false;

            // 压缩后的结构：[原始长度VarInt] [压缩数据]
            ProtocolCraft::WriteContainer originalLenBytes;
            ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(static_cast<int>(packetData.size()), originalLenBytes);
            packetData.clear();
            packetData.insert(packetData.end(), originalLenBytes.begin(), originalLenBytes.end());
            packetData.insert(packetData.end(), compressed.begin(), compressed.end());
        } else {
            // 未压缩，结构：[VarInt(0)] [包ID] [payload]
            LOGI("Packet %d below threshold, sending uncompressed", packetId);
            std::vector<uint8_t> uncompressed;
            uncompressed.push_back(0); // VarInt(0)
            uncompressed.insert(uncompressed.end(), packetData.begin(), packetData.end());
            packetData = uncompressed;
        }
    } else {
        // 未启用压缩，结构：[包ID] [payload]
        ProtocolCraft::WriteContainer idBytes;
        ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(packetId, idBytes);
        packetData.insert(packetData.end(), idBytes.begin(), idBytes.end());
        packetData.insert(packetData.end(), payload.begin(), payload.end());
    }

    // 手动添加包长度前缀
    ProtocolCraft::WriteContainer lenBytes;
    ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(static_cast<int>(packetData.size()), lenBytes);
    std::vector<uint8_t> fullPacket;
    fullPacket.insert(fullPacket.end(), lenBytes.begin(), lenBytes.end());
    fullPacket.insert(fullPacket.end(), packetData.begin(), packetData.end());

    printBytes(fullPacket, "Sending");
    int sent = send(sock, fullPacket.data(), fullPacket.size(), 0);
    return sent == (int)fullPacket.size();
}

bool NetworkManager::sendRawPacket(const std::vector<uint8_t>& fullPacketData) {
    if (!connected || fullPacketData.empty()) return false;

    std::vector<uint8_t> packetData;

    if (Compression::isEnabled()) {
        int threshold = Compression::getThreshold();
        LOGI("Sending raw packet, size=%d, compression enabled, threshold=%d",
             static_cast<int>(fullPacketData.size()), threshold);

        // fullPacketData 已经包含了 Packet ID
        if ((int)fullPacketData.size() >= threshold) {
            // 需要压缩
            LOGI("Compressing raw packet, uncompressed size=%d", static_cast<int>(fullPacketData.size()));
            std::vector<uint8_t> compressed = Compression::compress(fullPacketData);
            if (compressed.empty()) return false;

            // 压缩后的结构：[原始长度VarInt] [压缩数据]
            ProtocolCraft::WriteContainer originalLenBytes;
            ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(static_cast<int>(fullPacketData.size()), originalLenBytes);
            packetData.insert(packetData.end(), originalLenBytes.begin(), originalLenBytes.end());
            packetData.insert(packetData.end(), compressed.begin(), compressed.end());
        } else {
            // 未压缩，结构：[VarInt(0)] [Packet ID + payload]
            LOGI("Raw packet below threshold, sending uncompressed");
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

    printBytes(finalPacket, "Sending raw");
    int sent = send(sock, finalPacket.data(), finalPacket.size(), 0);
    return sent == (int)finalPacket.size();
}

std::vector<uint8_t> NetworkManager::receivePacket() {
    if (!connected) return {};

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
            std::vector<uint8_t> rest(iter, rawData.end());
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

bool NetworkManager::isConnected() const { return connected; }