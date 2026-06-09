#include "NetworkManager.h"
#include "AESEncrypter.h"
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

bool NetworkManager::isConnected() const { return connected; }

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
                return rest;
            } else {
                return Compression::decompress(rest, uncompressedLen);
            }
        } catch (...) {
            return {};
        }
    } else {
        return rawData;
    }
}