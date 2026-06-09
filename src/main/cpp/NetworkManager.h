#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

class AESEncrypter;

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();
    bool connect(const std::string& host, int port);
    void disconnect();
    bool sendRawPacket(const std::vector<uint8_t>& fullPacketData); // 发送已包含 Packet ID 的完整数据
    std::vector<uint8_t> receivePacket();
    bool isConnected() const;

    /**
     * 设置 AES 加密器（在 Encryption Request 处理后调用）
     * 加密器由调用方（ClientEngine）所有，NetworkManager 不负责释放
     * 发送时自动加密，接收时自动解密 — 参考 Botcraft TCP_Com
     */
    void setEncrypter(AESEncrypter* encrypter);
    bool isEncrypted() const;

private:
    std::vector<uint8_t> receiveEncryptedPacket();
    int sock;
    bool connected;
    AESEncrypter* encrypter;  // 不拥有所有权
};