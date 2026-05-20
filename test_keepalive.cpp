#include <iostream>
#include <vector>
#include <cstdint>
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundKeepAlivePacket.hpp"

int main() {
    // 服务器发送的 KeepAlive 数据（8字节大端序 Long）
    // 值：0x0EE294BC = 249730236
    std::vector<uint8_t> data = {
        0x00, 0x00, 0x00, 0x00, 0x0E, 0xE2, 0x94, 0xBC
    };
    
    std::cout << "Testing ProtocolCraft KeepAlive parsing..." << std::endl;
    std::cout << "Input data (8 bytes): ";
    for (size_t i = 0; i < data.size(); i++) {
        printf("%02X ", data[i]);
    }
    std::cout << std::endl;
    
    // 使用 ProtocolCraft 解析
    ProtocolCraft::ClientboundKeepAlivePacket packet;
    auto iter = data.cbegin();
    size_t length = data.size();
    
    try {
        packet.Read(iter, length);
        long long id = packet.GetId();
        
        std::cout << "Parsed ID: " << id << " (0x" << std::hex << id << std::dec << ")" << std::endl;
        std::cout << "Expected ID: 249730236 (0xEE294BC)" << std::endl;
        
        if (id == 249730236) {
            std::cout << "✓ PASS: ProtocolCraft correctly parsed the KeepAlive ID" << std::endl;
            return 0;
        } else {
            std::cout << "✗ FAIL: ProtocolCraft parsed incorrect ID!" << std::endl;
            std::cout << "This confirms the bug in ProtocolCraft." << std::endl;
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
