#pragma once
#include <string>
#include <vector>
#include <cstdint>

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();
    bool connect(const std::string& host, int port);
    void disconnect();
    bool sendPacket(int packetId, const std::vector<uint8_t>& payload);
    bool sendRawPacket(const std::vector<uint8_t>& fullPacketData); // 发送已包含 Packet ID 的完整数据
    std::vector<uint8_t> receivePacket();
    bool isConnected() const;
private:
    int sock;
    bool connected;
};