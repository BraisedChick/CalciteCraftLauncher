#include "ClientEngine.h"
#include "NetworkManager.h"
#include "Compression.h"
#include "ChunkManager.h"
#include "GLRenderer.h"
#include "utils.h"
#include "MinecraftVersion.h"
#include "CameraController.h"

// ProtocolCraft 头文件
#include "protocolCraft/include/protocolCraft/BinaryReadWrite.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Handshake/Serverbound/ServerboundClientIntentionPacket.hpp"

// ProtocolCraft 头文件 - 登录阶段
#include "protocolCraft/include/protocolCraft/Packets/Login/Serverbound/ServerboundHelloPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Login/Clientbound/ClientboundLoginCompressionPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Login/Clientbound/ClientboundGameProfilePacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Login/Clientbound/ClientboundLoginDisconnectPacket.hpp"

// ProtocolCraft 头文件 - 游戏阶段
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundLevelChunkWithLightPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundKeepAlivePacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Serverbound/ServerboundKeepAlivePacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundPlayerPositionPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Serverbound/ServerboundAcceptTeleportationPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Serverbound/ServerboundMovePlayerPacketPosRot.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Serverbound/ServerboundClientInformationPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundLoginPacket.hpp"

#include <vector>
#include <string>

ClientEngine::ClientEngine() : chunkManager(nullptr) {}

ClientEngine::~ClientEngine() = default;

bool ClientEngine::start(const std::string& host, int port, const std::string& username) {
    LOGI("========== Starting client ==========");
    LOGI("Server: %s:%d", host.c_str(), port);
    LOGI("Username: %s", username.c_str());

    // 初始化压缩状态
    Compression::setEnabled(false);
    Compression::setThreshold(-1);
    Compression::setReceiveEnabled(false);

    chunkManager = std::make_unique<ChunkManager>();

    NetworkManager net;
    if (!net.connect(host, port)) {
        LOGE("Failed to connect to %s:%d", host.c_str(), port);
        return false;
    }
    LOGI("Network connection established");

    // ========== 握手阶段 ==========
    {
        LOGI("Sending handshake packet via ProtocolCraft");

        // 从 VersionManager 获取协议版本（由启动器设置）
        int protocolVersion = VersionManager::getInstance().getProtocolVersion();
        if (protocolVersion == 0) {
            // 如果没有设置，默认使用 1.18.2
            protocolVersion = 758;
            VersionManager::getInstance().setProtocolVersion(protocolVersion);
            LOGW("Protocol version not set, using default: %d (1.18.2)", protocolVersion);
        }

        ProtocolCraft::ServerboundClientIntentionPacket handshake;
        handshake.SetProtocolVersion(protocolVersion);
        handshake.SetHostName(host);
        handshake.SetPort(port);
        handshake.SetIntention(2);  // 2 = LOGIN state

        LOGI("Using protocol version: %d (%s)",
             protocolVersion,
             VersionManager::getInstance().getVersionName().c_str());

        ProtocolCraft::WriteContainer writeData;
        handshake.Write(writeData);

        LOGI("Handshake packet size: %zu bytes", writeData.size());
        if (!net.sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()))) {
            LOGE("Failed to send handshake");
            net.disconnect();
            return false;
        }
    }

    // ========== 登录阶段 - 发送 Login Start ==========
    {
        LOGI("Sending login start: %s", username.c_str());

        ProtocolCraft::ServerboundHelloPacket loginStart;
        loginStart.SetGameProfile(username);  // 协议 758 使用 GameProfile 字段

        ProtocolCraft::WriteContainer writeData;
        loginStart.Write(writeData);

        LOGI("LoginStart packet size: %zu bytes", writeData.size());
        if (!net.sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()))) {
            LOGE("Failed to send login start");
            net.disconnect();
            return false;
        }
    }

    // ========== 登录阶段 - 接收响应 ==========
    while (true) {
        auto resp = net.receivePacket();
        if (resp.empty()) {
            LOGE("Empty response during login");
            net.disconnect();
            return false;
        }

        // 使用 VarInt 读取 Packet ID
        size_t pos = 0;
        ProtocolCraft::ReadIterator iter = resp.cbegin();
        size_t remaining = resp.size();
        int pid = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, remaining);
        pos = resp.size() - remaining;  // 更新已读取的位置

        if (pid == 0x02) {
            // Login Success (Game Profile)
            LOGI("Login success!");

            ProtocolCraft::ClientboundGameProfilePacket successPacket;
            std::vector<unsigned char> packetData(resp.begin() + pos, resp.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();

            try {
                successPacket.Read(iter, length);
                LOGI("Logged in as: %s", successPacket.GetUsername().c_str());
            } catch (const std::exception& e) {
                LOGE("Failed to parse login success: %s", e.what());
            }

            break;
        } else if (pid == 0x03) {
            // Set Compression
            ProtocolCraft::ClientboundLoginCompressionPacket compressionPacket;
            std::vector<unsigned char> packetData(resp.begin() + pos, resp.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();

            compressionPacket.Read(iter, length);

            int threshold = compressionPacket.GetCompressionThreshold();
            LOGI("Enable compression, threshold=%d", threshold);
            Compression::setReceiveEnabled(true);
            Compression::setThreshold(threshold);
            continue;
        } else if (pid == 0x00) {
            // Disconnect
            ProtocolCraft::ClientboundLoginDisconnectPacket disconnectPacket;
            std::vector<unsigned char> packetData(resp.begin() + pos, resp.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();

            try {
                disconnectPacket.Read(iter, length);
                // Reason 是 Chat 类型，需要转换为字符串
                LOGE("Disconnected during login");
            } catch (...) {
                LOGE("Disconnected during login (failed to parse reason)");
            }
            net.disconnect();
            return false;
        } else {
            LOGE("Unexpected login packet: %d", pid);
            net.disconnect();
            return false;
        }
    }

    // 启用发送压缩
    if (Compression::isReceiveEnabled()) {
        Compression::setEnabled(true);
        LOGI("Compression fully enabled");
    }

    // ========== 发送客户端信息（视野距离等）==========
    // 必须在进入 PLAY 状态后立即发送，让服务器知道客户端的视野距离
    {
        ProtocolCraft::ServerboundClientInformationPacket infoPacket;
        infoPacket.SetLanguage("en_US");
        infoPacket.SetViewDistance(10);    // 请求 10 个区块的视野距离
        infoPacket.SetChatVisibility(0);   // 0=全部显示
        infoPacket.SetChatColors(true);
        infoPacket.SetModelCustomisation(0x7F);  // 全部启用
        infoPacket.SetMainHand(1);         // 1=右手
        infoPacket.SetTextFilteringEnabled(false);
        infoPacket.SetAllowListing(true);

        ProtocolCraft::WriteContainer writeData;
        infoPacket.Write(writeData);
        net.sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
        LOGI("Sent Client Information (ViewDistance=10)");
    }

    // ========== PLAY 状态主循环 ==========
    while (true) {
        auto resp = net.receivePacket();
        if (resp.empty()) {
            LOGI("Connection closed");
            break;
        }

        // 使用 VarInt 读取 Packet ID
        size_t pos = 0;
        ProtocolCraft::ReadIterator iter = resp.cbegin();
        size_t remaining = resp.size();
        int pid = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, remaining);
        pos = resp.size() - remaining;  // 更新已读取的位置

        handlePlayPacket(net, pid, resp, pos);
    }

    net.disconnect();
    return true;
}

void ClientEngine::handlePlayPacket(NetworkManager& net, int packetId,
                                    const std::vector<uint8_t>& data, size_t startPos) {
    try {
        switch (packetId) {
            case 0x25: { // Login (Play) - 玩家初始状态
                LOGI("Received ClientboundLoginPacket (Play)");

                ProtocolCraft::ClientboundLoginPacket loginPacket;
                std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
                auto iter = packetData.cbegin();
                size_t length = packetData.size();

                try {
                    loginPacket.Read(iter, length);
                    LOGI("Player ID: %d, GameType: %d",
                         loginPacket.GetPlayerId(),
                         loginPacket.GetGameType());

                    // Minecraft 1.18+ standard Overworld dimension
                    // min_y = -64, height = 384 (Y range: -64 to 320)
                    // TODO: Parse DimensionType NBT from Login packet to get exact values
                    dimensionMinY = -64;
                    dimensionHeight = 384;
                    LOGI("Dimension info: min_y=%d, height=%d (Y range: %d to %d)",
                         dimensionMinY, dimensionHeight, dimensionMinY, dimensionMinY + dimensionHeight - 1);
                } catch (const std::exception& e) {
                    LOGE("Failed to parse Login packet: %s", e.what());
                }
                break;
            }

            case 0x21: { // Keep Alive (Clientbound)
                if (data.size() - startPos >= 8) {
                    // 手动解析 8 字节大端序 Long（ProtocolCraft 有 bug）
                    long long keepAliveId = 0;
                    for (int i = 0; i < 8; i++) {
                        keepAliveId = (keepAliveId << 8) | data[startPos + i];
                    }

                    // 手动构造响应包：Packet ID (VarInt) + KeepAlive ID (8 bytes Big Endian)
                    std::vector<uint8_t> response;

                    // Packet ID: 0x0F (Serverbound KeepAlive)
                    response.push_back(0x0F);

                    // KeepAlive ID: 8 bytes Big Endian
                    for (int i = 7; i >= 0; i--) {
                        response.push_back((keepAliveId >> (i * 8)) & 0xFF);
                    }

                    bool sent = net.sendRawPacket(response);
                    if (!sent) {
                        LOGE("Failed to send KeepAlive response!");
                    }
                }
                break;
            }

            case 0x38: { // Player Position And Look
                ProtocolCraft::ClientboundPlayerPositionPacket posPacket;
                std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
                auto iter = packetData.cbegin();
                size_t length = packetData.size();

                posPacket.Read(iter, length);

                // 保存玩家位置
                playerX = posPacket.GetX();
                playerY = posPacket.GetY();
                playerZ = posPacket.GetZ();
                yaw = posPacket.GetYRot();
                pitch = posPacket.GetXRot();
                hasPosition = true;

                LOGI("Received teleport request, ID=%d, pos=(%.2f, %.2f, %.2f), yaw=%.2f, pitch=%.2f",
                     posPacket.GetId_(), playerX, playerY, playerZ, yaw, pitch);

                // 直接同步摄像机位置（每次收到都更新，支持传送、重生等）
                CameraController::getInstance().setPosition(playerX, playerY, playerZ);
                CameraController::getInstance().setRotation(pitch, yaw);

                LOGI("Camera synced to player position");

                // 发送 Teleport Confirm
                ProtocolCraft::ServerboundAcceptTeleportationPacket confirmPacket;
                confirmPacket.SetId_(posPacket.GetId_());

                ProtocolCraft::WriteContainer writeData;
                confirmPacket.Write(writeData);

                LOGI("Sending TeleportConfirm with ID=%d", posPacket.GetId_());
                net.sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));

                // 立即发送位置/姿态包，确认传送完成
                ProtocolCraft::ServerboundMovePlayerPacketPosRot movePacket;
                movePacket.SetX(posPacket.GetX());
                movePacket.SetY(posPacket.GetY());
                movePacket.SetZ(posPacket.GetZ());
                movePacket.SetYRot(posPacket.GetYRot());
                movePacket.SetXRot(posPacket.GetXRot());
                movePacket.SetOnGround(true);

                ProtocolCraft::WriteContainer moveData;
                movePacket.Write(moveData);

                LOGI("Sending MovePlayerPacket after teleport");
                net.sendRawPacket(std::vector<uint8_t>(moveData.begin(), moveData.end()));
                break;
            }

            case 0x22: { // Chunk Data (Level Chunk with Light)
                ProtocolCraft::ClientboundLevelChunkWithLightPacket chunkPacket;

                std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
                auto iter = packetData.cbegin();
                size_t length = packetData.size();

                try {
                    chunkPacket.Read(iter, length);

                    int chunkX = chunkPacket.GetX();
                    int chunkZ = chunkPacket.GetZ();

                    // Extract primary bit mask from the raw packet data (before ProtocolCraft parsing)
                    // Packet structure: [Packet ID] [X: int] [Z: int] [Primary Bit Mask: long] [Heightmaps NBT] [Sections...]
                    // ProtocolCraft consumed: Packet ID, X, Z, Primary Bit Mask
                    // GetBuffer() returns: Heightmaps NBT + Sections + ...
                    // So we need to extract bitmask from the original packetData

                    auto rawIter = packetData.cbegin();

                    // Skip Packet ID (VarInt) - but we already know it's 0x22
                    // Actually, the packetData starts AFTER the packet ID in handlePlayPacket
                    // So we just need to skip X and Z

                    rawIter += 8;  // Skip X (4 bytes) and Z (4 bytes)

                    // Now read the 8-byte bitmask (big-endian long long)
                    long long extractedBitMask = 0;
                    for (int i = 0; i < 8; i++) {
                        extractedBitMask = (extractedBitMask << 8) | *rawIter;
                        ++rawIter;
                    }

                    const auto& chunkData = chunkPacket.GetChunkData();
                    const auto& buffer_data = chunkData.GetBuffer();

                    if (chunkManager && !buffer_data.empty()) {
                        std::vector<uint8_t> rawData(buffer_data.begin(), buffer_data.end());

                        std::vector<uint8_t> emptyHeightmaps;
                        std::vector<uint8_t> emptyBlockEntities;

                        chunkManager->loadChunk(
                                chunkX, chunkZ,
                                rawData,
                                true,
                                extractedBitMask,  // Use the extracted bitmask!
                                emptyHeightmaps,
                                emptyBlockEntities,
                                dimensionMinY
                        );

                        // 通知渲染器重建网格（增量更新，只重建当前区块）
                        if (glRenderer) {
                            glRenderer->setChunkManager(chunkManager.get());
                            glRenderer->markChunkForUpdate(chunkX, chunkZ);
                        }
                    }
                } catch (const std::exception& e) {
                    LOGE("Failed to parse chunk packet: %s", e.what());
                }
                break;
            }

            default: {
                // 静默忽略未处理的数据包
                break;
            }
        }
    } catch (const std::exception& e) {
        LOGE("Error handling packet %d: %s", packetId, e.what());
    }
}

size_t ClientEngine::calculateNBTSize(const std::vector<uint8_t>& data, size_t startPos) {
    return 0;
}

void ClientEngine::parseChunkDataPacket(const std::vector<uint8_t>& data, size_t startPos) {
    // Deprecated function
}