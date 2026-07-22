#include "ClientEngine.h"
#include "NetworkManager/NetworkManager.h"
#include "AESEncrypter.h"
#include "Compression.h"
#include "ChunkManager.h"
#include "GLRenderer.h"
#include "utils.h"
#include "MinecraftVersion.h"
#include "CameraController.h"
#include "Collision.h"

// ProtocolCraft 头文件
#include "protocolCraft/BinaryReadWrite.hpp"
#include "protocolCraft/Packets/Handshake/Serverbound/ServerboundClientIntentionPacket.hpp"

// ProtocolCraft 头文件 - 登录阶段
#include "protocolCraft/Packets/Login/Serverbound/ServerboundHelloPacket.hpp"
#include "protocolCraft/Packets/Login/Clientbound/ClientboundLoginCompressionPacket.hpp"
#include "protocolCraft/Packets/Login/Clientbound/ClientboundGameProfilePacket.hpp"
#include "protocolCraft/Packets/Login/Clientbound/ClientboundLoginDisconnectPacket.hpp"

// ProtocolCraft 头文件 - 游戏阶段
#include "protocolCraft/Packets/Game/Clientbound/ClientboundLevelChunkWithLightPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundKeepAlivePacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundKeepAlivePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundPlayerPositionPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundAcceptTeleportationPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundMovePlayerPacketPosRot.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundMovePlayerPacketStatusOnly.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundClientInformationPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundSetCarriedItemPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundContainerClickPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundContainerClosePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSetExperiencePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSetCarriedItemPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundUseItemOnPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundUseItemPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundPlayerActionPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundInteractPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundClientCommandPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundLoginPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundBlockUpdatePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundLightUpdatePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSectionBlocksUpdatePacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundContainerSetContentPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundContainerSetSlotPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSetHealthPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundPlayerCombatKillPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundGameEventPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundRespawnPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSetTimePacket.hpp"
// 聊天包
#include "protocolCraft/Packets/Game/Serverbound/ServerboundChatPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundChatPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSystemChatPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundPlayerChatPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundDisguisedChatPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundAddEntityPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundAddMobPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundAddPlayerPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundMoveEntityPacketPosRot.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundMoveEntityPacketPos.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundMoveEntityPacketRot.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundTeleportEntityPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundRemoveEntitiesPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundSetEntityMotionPacket.hpp"
#include "protocolCraft/Packets/Game/Clientbound/ClientboundOpenScreenPacket.hpp"
#include "Light.h"
#include "EntityManager.h"
#include "EntityRenderer.h"
#include "protocolCraft/Types/NBT/Tag.hpp"
#include "protocolCraft/Utilities/Json.hpp"
#include "3rdparty/json.hpp"
#include "BiomeColorManager.h"
#include "BlockRegistry.h"
#include "PlayerInventory.h"
#include "gui/GameUI.h"

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <cmath>
#include <chrono>
#include <sstream>

using njson = nlohmann::json;

ClientEngine* ClientEngine::instance = nullptr;

// 前向声明：在 native-lib.cpp 中定义，用于通过 JNI 调用 Java 层处理完整加密请求
// Java 层负责：生成共享密钥 + SHA1 哈希 + Session Join + RSA 加密
extern bool callJavaHandleEncryptionRequest(
    const std::string& serverID,
    const std::vector<unsigned char>& publicKey,
    const std::vector<unsigned char>& verifyToken,
    std::vector<unsigned char>& sharedSecret,
    std::vector<unsigned char>& encryptedSecret,
    std::vector<unsigned char>& encryptedVerifyToken);

ClientEngine::ClientEngine()
    : chunkManager(nullptr),
      m_inventory(std::make_unique<PlayerInventory>()),
      m_entityManager(std::make_unique<EntityManager>()),
      m_entityRenderer(std::make_unique<EntityRenderer>()) {
    instance = this;
}

ClientEngine::~ClientEngine() = default;

void ClientEngine::setAuthInfo(const std::string& accessToken, const std::string& uuid, const std::string& tokenType) {
    this->accessToken = accessToken;
    playerUuid = uuid;
    this->tokenType = tokenType;
    premium = !accessToken.empty();
    LOGI("Auth info set: premium=%d, uuid=%s", premium, playerUuid.c_str());
}

bool ClientEngine::start(const std::string& host, int port, const std::string& username) {
    LOGI("========== Starting client ==========");
    LOGI("Server: %s:%d", host.c_str(), port);
    LOGI("Username: %s", username.c_str());

    // 初始化压缩状态
    Compression::setEnabled(false);
    Compression::setThreshold(-1);
    Compression::setReceiveEnabled(false);

    chunkManager = std::make_unique<ChunkManager>();

    // chunkManager 刚创建，通知相关模块更新指针
    // （native-lib.cpp 在 start() 之前调用 setChunkManager 时 chunkManager 还是 nullptr）
    if (glRenderer) {
        glRenderer->setChunkManager(chunkManager.get());
    }
    Collision::getInstance().setChunkManager(chunkManager.get());
    Light::getInstance().setChunkManager(chunkManager.get());
    LOGI("ChunkManager created and linked to renderer/collision/light");

    net = std::make_unique<NetworkManager>();
    if (!net->connect(host, port)) {
        LOGE("Failed to connect to %s:%d", host.c_str(), port);
        return false;
    }
    LOGI("Network connection established");

    // ========== 握手阶段 ==========
    {
        LOGI("Sending handshake packet via ProtocolCraft");

        // 从 VersionManager 获取协议版本（由 Java 层版本选择设置）
        int protocolVersion = VersionManager::getInstance().getProtocolVersion();
        if (protocolVersion == 0) {
            LOGE("Protocol version not set! Please select a version in launcher");
            net->disconnect();
            return false;
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
        if (!sendPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()))) {
            LOGE("Failed to send handshake");
            net->disconnect();
            return false;
        }
    }

    // ========== 登录阶段 - 发送 Login Start ==========
    {
        LOGI("Sending login start: %s", username.c_str());

        ProtocolCraft::ServerboundHelloPacket loginStart;
        #if PROTOCOL_VERSION < 759
                loginStart.SetGameProfile(username);  // 1.18.2: 直接设置用户名
        #else
                loginStart.SetName_(username);  // 1.19+: 设置 Name_ 字段
        #endif

        ProtocolCraft::WriteContainer writeData;
        loginStart.Write(writeData);

        LOGI("LoginStart packet size: %zu bytes", writeData.size());
        if (!sendPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()))) {
            LOGE("Failed to send login start");
            net->disconnect();
            return false;
        }
    }

    // ========== 登录阶段 - 接收响应 ==========
    while (true) {
        auto resp = net->receivePacket();
        if (resp.empty()) {
            LOGE("Empty response during login");
            net->disconnect();
            return false;
        }

        // 使用 VarInt 读取 Packet ID
        size_t pos = 0;
        ProtocolCraft::ReadIterator iter = resp.cbegin();
        size_t remaining = resp.size();
        int pid = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, remaining);
        pos = resp.size() - remaining;  // 更新已读取的位置

        switch (pid) {
        case 0x02: {
            // Login Success (Game Profile)
            LOGI("Login success!");

            ProtocolCraft::ClientboundGameProfilePacket successPacket;
            std::vector<unsigned char> packetData(resp.begin() + pos, resp.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();

            try {
                successPacket.Read(iter, length);
                #if PROTOCOL_VERSION < 759
                                LOGI("Logged in as: %s", successPacket.GetUsername().c_str());
                #else
                                LOGI("Logged in as: %s", successPacket.GetGameProfile().GetName().c_str());
                #endif
            } catch (const std::exception& e) {
                LOGE("Failed to parse login success: %s", e.what());
            }

            goto loginDone; //登录结束，直接跳出循环
        }

        case 0x01: {
            // Encryption Request (Hello) — 在线模式服务器
            LOGI("Received Encryption Request (online mode server)");

            if (!premium) {
                LOGE("Server is in online mode, but no premium auth available");
                net->disconnect();
                return false;
            }

            std::vector<unsigned char> packetData(resp.begin() + pos, resp.end());
            auto dataIter = packetData.cbegin();
            size_t dataLen = packetData.size();

            try {
                int serverIdLen = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(dataIter, dataLen);
                std::string serverID(serverIdLen, '\0');
                for (int i = 0; i < serverIdLen; ++i) {
                    serverID[i] = ProtocolCraft::ReadData<char>(dataIter, dataLen);
                }

                int pubKeyLen = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(dataIter, dataLen);
                std::vector<unsigned char> publicKey(pubKeyLen);
                for (int i = 0; i < pubKeyLen; ++i) {
                    publicKey[i] = static_cast<unsigned char>(ProtocolCraft::ReadData<char>(dataIter, dataLen));
                }

                int verifyTokenLen = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(dataIter, dataLen);
                std::vector<unsigned char> verifyToken(verifyTokenLen);
                for (int i = 0; i < verifyTokenLen; ++i) {
                    verifyToken[i] = static_cast<unsigned char>(ProtocolCraft::ReadData<char>(dataIter, dataLen));
                }

                LOGI("Encryption Request: serverID_len=%d, pubKey_len=%d, verifyToken_len=%d",
                     serverIdLen, pubKeyLen, verifyTokenLen);

                std::vector<unsigned char> rawSharedSecret;
                std::vector<unsigned char> encryptedSharedSecret;
                std::vector<unsigned char> encryptedVerifyToken;

                bool encResult = callJavaHandleEncryptionRequest(
                    serverID, publicKey, verifyToken,
                    rawSharedSecret, encryptedSharedSecret, encryptedVerifyToken);

                if (!encResult || rawSharedSecret.empty()) {
                    LOGE("Failed to handle encryption request via Java");
                    net->disconnect();
                    return false;
                }
                LOGI("Java encryption handling successful, sharedSecret_len=%zu, encSecret_len=%zu, encVerifyToken_len=%zu",
                     rawSharedSecret.size(), encryptedSharedSecret.size(), encryptedVerifyToken.size());

                aesEncrypter = std::make_unique<AESEncrypter>();
                aesEncrypter->Init(rawSharedSecret);

                if (!aesEncrypter->isInitialized()) {
                    LOGE("Failed to initialize AESEncrypter");
                    net->disconnect();
                    return false;
                }

                ProtocolCraft::WriteContainer keyPacket;
                ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(0x01, keyPacket);
                ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(
                    static_cast<int>(encryptedSharedSecret.size()), keyPacket);
                for (auto b : encryptedSharedSecret) {
                    ProtocolCraft::WriteData<char>(static_cast<char>(b), keyPacket);
                }
                ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(
                    static_cast<int>(encryptedVerifyToken.size()), keyPacket);
                for (auto b : encryptedVerifyToken) {
                    ProtocolCraft::WriteData<char>(static_cast<char>(b), keyPacket);
                }

                if (!sendPacket(std::vector<uint8_t>(keyPacket.begin(), keyPacket.end()))) {
                    LOGE("Failed to send encryption key response");
                    net->disconnect();
                    return false;
                }
                LOGI("Encryption key response sent");

                net->setEncrypter(aesEncrypter.get());
                LOGI("AES-128-CFB8 stream encryption enabled on NetworkManager");

            } catch (const std::exception& e) {
                LOGE("Failed to parse encryption request: %s", e.what());
                net->disconnect();
                return false;
            }
            continue;  // 继续等待 Login Success
        }

        case 0x03: {
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
        }

        case 0x00: {
            // Disconnect
            ProtocolCraft::ClientboundLoginDisconnectPacket disconnectPacket;
            std::vector<unsigned char> packetData(resp.begin() + pos, resp.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();

            try {
                disconnectPacket.Read(iter, length);
                LOGE("Disconnected during login");
            } catch (...) {
                LOGE("Disconnected during login (failed to parse reason)");
            }
            net->disconnect();
            return false;
        }

        default: {
            LOGE("Unexpected login packet: %d", pid);
            net->disconnect();
            return false;
        }
        }
    }

loginDone:
    // 启用发送压缩（参照 Botcraft：收到 Set Compression 后立即启用收发压缩）
    // Botcraft 用单一 compression 变量控制，我们在登录循环结束后统一启用
    if (Compression::isReceiveEnabled()) {
        Compression::setEnabled(true);
        LOGI("Compression fully enabled (threshold=%d)", Compression::getThreshold());
    } else {
        LOGI("Compression not enabled by server (offline mode)");
    }

    // 注意：不在这里发送 ClientInformation！
    // 某些服务器在 Login Success 之后需要时间切换到 PLAY 状态
    // 必须等收到服务器的第一个 PLAY 状态包之后再发送
    bool clientInfoSent = false;

    // 进入 PLAY 状态（注意：不在这里启用移动发送，必须等收到第一个 0x38 确保坐标正确）
    // 移动包的启用放在 handlePlayPacket 的 0x38 分支中

    // 委托 NetworkManager 处理 PLAY 状态
    net->setEngine(this);
    net->registerHandlers();
    net->startPlayLoop();  // 阻塞直到连接关闭

    net->disconnect();
    return true;
}

bool ClientEngine::sendPacket(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net) return false;
    return net->sendRawPacket(data);
}

bool ClientEngine::isConnected() const {
    std::lock_guard<std::mutex> lock(netMutex);
    return net && net->isConnected();
}

void ClientEngine::sendPlayerMovement(double x, double y, double z, float yaw, float pitch, bool onGround) {
    if (!movementEnabled.load()) return;
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    // 限速 20 次/秒（50ms 间隔），匹配原版游戏刻速率
    auto now = std::chrono::steady_clock::now();
    auto msSinceLastSend = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMoveSendTime).count();
    if (msSinceLastSend < 50) return;

    bool posChanged = !lastSent.initialized ||
                      fabs(x - lastSent.x) > 0.001 ||
                      fabs(y - lastSent.y) > 0.001 ||
                      fabs(z - lastSent.z) > 0.001;
    bool rotChanged = !lastSent.initialized ||
                      fabs(yaw - lastSent.yaw) > 0.001f ||
                      fabs(pitch - lastSent.pitch) > 0.001f;

    if (posChanged || rotChanged) {
        // 位置或旋转变化时发送完整移动包
        ProtocolCraft::ServerboundMovePlayerPacketPosRot movePacket;
        movePacket.SetX(x);
        movePacket.SetY(y);
        movePacket.SetZ(z);
        float yawDeg = glm::degrees(yaw);
        if (yawDeg > 180.0f) yawDeg -= 360.0f;
        movePacket.SetYRot(yawDeg);
        movePacket.SetXRot(glm::degrees(pitch));
        movePacket.SetOnGround(onGround);

        ProtocolCraft::WriteContainer writeData;
        movePacket.Write(writeData);
        net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));

        lastSent.x = x;
        lastSent.y = y;
        lastSent.z = z;
        lastSent.yaw = yaw;
        lastSent.pitch = pitch;
        lastSent.onGround = onGround;
        lastSent.initialized = true;
        lastMoveSendTime = now;
        return;
    }

    // 位置未变，每 500ms 发送一次 StatusOnly 同步地面状态
    if (msSinceLastSend >= 500) {
        ProtocolCraft::ServerboundMovePlayerPacketStatusOnly statusPacket;
        statusPacket.SetOnGround(onGround);

        ProtocolCraft::WriteContainer writeData;
        statusPacket.Write(writeData);
        net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
        lastSent.onGround = onGround;
        lastMoveSendTime = now;
    }
}

void ClientEngine::sendHeldItemChange(int slot) {
    if (slot < 0 || slot > 8) return;
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::ServerboundSetCarriedItemPacket heldPacket;
    heldPacket.SetSlot(slot);

    ProtocolCraft::WriteContainer writeData;
    heldPacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
}

void ClientEngine::sendBlockPlacement(int blockX, int blockY, int blockZ, int face, int hand) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::ServerboundUseItemOnPacket placePacket;

    ProtocolCraft::NetworkPosition pos;
    pos.SetX(blockX);
    pos.SetY(blockY);
    pos.SetZ(blockZ);
    placePacket.SetLocation(pos);

    placePacket.SetHand(hand);
    placePacket.SetDirection(face);
    placePacket.SetCursorPositionX(0.5f);
    placePacket.SetCursorPositionY(0.5f);
    placePacket.SetCursorPositionZ(0.5f);
    placePacket.SetInside(false);

    ProtocolCraft::WriteContainer writeData;
    placePacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
}

void ClientEngine::sendUseItem(int hand) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::ServerboundUseItemPacket usePacket;
    usePacket.SetHand(hand);

    ProtocolCraft::WriteContainer writeData;
    usePacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
    LOGI("Sent UseItem: hand=%d", hand);
}

void ClientEngine::sendBlockBreakStart(int blockX, int blockY, int blockZ, int face) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::NetworkPosition pos;
    pos.SetX(blockX);
    pos.SetY(blockY);
    pos.SetZ(blockZ);

    // Action 0 = START_DIGGING (开始挖掘)
    ProtocolCraft::ServerboundPlayerActionPacket startDig;
    startDig.SetAction(0);
    startDig.SetPos(pos);
    startDig.SetDirection(static_cast<char>(face));

    ProtocolCraft::WriteContainer writeStart;
    startDig.Write(writeStart);
    net->sendRawPacket(std::vector<uint8_t>(writeStart.begin(), writeStart.end()));
}

void ClientEngine::sendBlockBreakFinish(int blockX, int blockY, int blockZ, int face) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::NetworkPosition pos;
    pos.SetX(blockX);
    pos.SetY(blockY);
    pos.SetZ(blockZ);

    // Action 2 = STOP_DESTROY_BLOCK (完成挖掘)
    ProtocolCraft::ServerboundPlayerActionPacket finishDig;
    finishDig.SetAction(2);
    finishDig.SetPos(pos);
    finishDig.SetDirection(static_cast<char>(face));

    ProtocolCraft::WriteContainer writeFinish;
    finishDig.Write(writeFinish);
    net->sendRawPacket(std::vector<uint8_t>(writeFinish.begin(), writeFinish.end()));
}

void ClientEngine::sendBlockBreakAbort(int blockX, int blockY, int blockZ, int face) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::NetworkPosition pos;
    pos.SetX(blockX);
    pos.SetY(blockY);
    pos.SetZ(blockZ);

    // Action 1 = ABORT_DESTROY_BLOCK (中断挖掘)
    ProtocolCraft::ServerboundPlayerActionPacket abortDig;
    abortDig.SetAction(1);
    abortDig.SetPos(pos);
    abortDig.SetDirection(static_cast<char>(face));

    ProtocolCraft::WriteContainer writeAbort;
    abortDig.Write(writeAbort);
    net->sendRawPacket(std::vector<uint8_t>(writeAbort.begin(), writeAbort.end()));
}

void ClientEngine::sendEntityAttack(int entityId) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::ServerboundInteractPacket packet;
    packet.SetEntityId(entityId);
#if PROTOCOL_VERSION < 775
    packet.SetAction(1);  // 1 = ATTACK
#endif
#if PROTOCOL_VERSION > 722
    packet.SetUsingSecondaryAction(false);
#endif

    ProtocolCraft::WriteContainer writeData;
    packet.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
    LOGI("Sent Interact(ATTACK): entityId=%d", entityId);
}

void ClientEngine::sendRespawn() {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::ServerboundClientCommandPacket cmdPacket;
    cmdPacket.SetAction(0);  // PERFORM_RESPAWN

    ProtocolCraft::WriteContainer writeData;
    cmdPacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
    deathMessage.clear();
    LOGI("Sent respawn request (ClientCommand PERFORM_RESPAWN)");
}

void ClientEngine::sendChatMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::ServerboundChatPacket chatPacket;
    chatPacket.SetMessage(message);
#if PROTOCOL_VERSION >= 759
    chatPacket.SetTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
#if PROTOCOL_VERSION < 760
    chatPacket.SetSignedPreview(false);
    // SaltSignature 用空签名
    ProtocolCraft::SaltSignature saltSig;
    saltSig.SetSalt(0);
    std::array<unsigned char, 32> emptySig{};
    saltSig.SetSignature(emptySig);
    chatPacket.SetSaltSignature(saltSig);
#else
    chatPacket.SetSalt(0);
    chatPacket.SetSignature(std::nullopt);
    ProtocolCraft::LastSeenMessagesUpdate lastSeen;
    lastSeen.SetOffset(0);
    lastSeen.SetAcknowledged(std::bitset<20>());
    chatPacket.SetLastSeenMessages(lastSeen);
#endif
#endif

    ProtocolCraft::WriteContainer writeData;
    chatPacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
    LOGI("Sent chat message: %s", message.c_str());
}

void ClientEngine::sendContainerClick(int slotNum, int button, int containerId) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    // 确定实际容器 ID
    if (containerId < 0) {
        int openId = GameUI::getInstance().getOpenContainerId();
        containerId = (openId >= 0) ? openId : 0;
    }

    auto& inv = *m_inventory;
    InvSlot cursor = inv.getCursorItem();
    // 根据容器ID读取正确的槽位数组
    InvSlot clicked = (containerId > 0) ? inv.getContainerSlot(slotNum) : inv.getSlot(slotNum);

    // 构建点击后的光标和槽位状态（客户端预测）
    InvSlot newCursor = cursor;
    InvSlot newClicked = clicked;

    if (button == 0) {  // 左键：拿取/放下/交换
        if (!cursor.present && clicked.present) {
            // 光标空，槽位有物品 → 拿起
            newCursor = clicked;
            newClicked = InvSlot{};
        } else if (cursor.present && !clicked.present) {
            // 光标有物品，槽位空 → 放下
            newClicked = cursor;
            newCursor = InvSlot{};
        } else if (cursor.present && clicked.present && cursor.itemId == clicked.itemId) {
            // 同类物品合并
            int total = cursor.count + clicked.count;
            int maxStack = 64;
            if (total <= maxStack) {
                newClicked.count = (int8_t)total;
                newCursor = InvSlot{};
            } else {
                newClicked.count = (int8_t)maxStack;
                newCursor.count = (int8_t)(total - maxStack);
            }
        } else if (cursor.present && clicked.present) {
            // 不同物品 → 交换
            newCursor = clicked;
            newClicked = cursor;
        }
    } else if (button == 1) {  // 右键：放一个/拿一半
        if (cursor.present && !clicked.present) {
            newClicked.present = true;
            newClicked.itemId = cursor.itemId;
            newClicked.count = 1;
            if (cursor.count > 1) {
                newCursor.count = cursor.count - 1;
            } else {
                newCursor = InvSlot{};
            }
        } else if (!cursor.present && clicked.present) {
            int half = clicked.count / 2;
            int remain = clicked.count - half;
            newCursor.present = true;
            newCursor.itemId = clicked.itemId;
            newCursor.count = (int8_t)half;
            if (remain > 0) {
                newClicked.count = (int8_t)remain;
            } else {
                newClicked = InvSlot{};
            }
        }
    }

    // 更新本地状态
    inv.setCursorItem(newCursor);
    if (containerId > 0) {
        inv.setContainerLocalSlot(slotNum, newClicked);
    } else {
        inv.setLocalSlot(slotNum, newClicked);
    }

    // 构建 ProtocolCraft Slot 对象
    auto toSlot = [](const InvSlot& is) -> ProtocolCraft::Slot {
        ProtocolCraft::Slot s;
        if (is.present && is.itemId > 0) {
            s.SetPresent(true);
            s.SetItemId(is.itemId);
            s.SetItemCount(is.count);
        }
        return s;
    };

    ProtocolCraft::ServerboundContainerClickPacket clickPacket;
    clickPacket.SetContainerId(containerId);
    clickPacket.SetStateId(inv.getStateId());
    clickPacket.SetSlotNum((short)slotNum);
    clickPacket.SetButtonNum((char)button);
    clickPacket.SetClickType(0);  // PICKUP

    // ChangedSlots: 告知服务器点击后槽位的新状态
    std::map<short, ProtocolCraft::Slot> changed;
    changed[(short)slotNum] = toSlot(newClicked);
    clickPacket.SetChangedSlots(changed);

    clickPacket.SetCarriedItem(toSlot(newCursor));

    ProtocolCraft::WriteContainer writeData;
    clickPacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
}

void ClientEngine::sendContainerClose() {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    int containerId = GameUI::getInstance().getOpenContainerId();
    if (containerId <= 0) return; // 0=玩家背包，不需要关闭包

    // 关闭容器前，将容器数据中的背包部分同步回 slots
    // 工作台容器布局: slots 10-36=主背包, 37-45=快捷栏
    // 玩家物品栏布局: slots 9-35=主背包, 36-44=快捷栏
    auto& inv = *m_inventory;
    const auto& containerSlots = inv.getContainerSlots();
    if (containerSlots.size() >= 46) {
        std::vector<InvSlot> updatedSlots(inv.getSlotCount());
        // 已有 slots 数据中保留 crafting/armor/offhand（0-8,45）
        for (int i = 0; i < inv.getSlotCount() && i < 46; i++) {
            if (i >= 0 && i < 9) {
                // slots 0-8: keep existing (2×2 craft + armor)
                updatedSlots[i] = inv.getSlot(i);
            } else if (i == 45) {
                updatedSlots[i] = inv.getSlot(i); // offhand
            }
        }
        // 主背包: containerSlots[10..36] → slots[9..35]
        for (int i = 0; i < 27 && 10 + i < (int)containerSlots.size() && 9 + i < (int)updatedSlots.size(); i++) {
            updatedSlots[9 + i] = containerSlots[10 + i];
        }
        // 快捷栏: containerSlots[37..45] → slots[36..44]
        for (int i = 0; i < 9 && 37 + i < (int)containerSlots.size() && 36 + i < (int)updatedSlots.size(); i++) {
            updatedSlots[36 + i] = containerSlots[37 + i];
        }
        inv.setContent(0, updatedSlots);
        LOGI("ContainerClose: synced %zu container slots to player inventory", containerSlots.size());
    }

    ProtocolCraft::ServerboundContainerClosePacket closePacket;
    closePacket.SetContainerId((unsigned char)containerId);

    ProtocolCraft::WriteContainer writeData;
    closePacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
    LOGI("Sent ContainerClose: id=%d", containerId);
}

void ClientEngine::sendContainerQuickCraft(int phase, int slotNum, int button) {
    // 原版MC QUICK_CRAFT 协议：
    // phase 0 = 开始拖拽（buttonNum = type<<2 | 0）
    // phase 1 = 拖过槽位（buttonNum = type<<2 | 1）
    // phase 2 = 结束拖拽（buttonNum = type<<2 | 2）
    // type: 0=均分(CHARITABLE), 1=每格1个(GREEDY), 2=复制(CLONE,创造模式)
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    auto& inv = *m_inventory;

    // 计算 buttonNum: (type << 2) | phase
    // type 0=左键均分, type 1=右键每格1个
    int type = (button == 0) ? 0 : 1;
    int buttonNum = (type << 2) | phase;

    ProtocolCraft::ServerboundContainerClickPacket clickPacket;
    // 使用当前打开的容器ID（0=玩家背包）
    int qcContainerId = GameUI::getInstance().getOpenContainerId();
    clickPacket.SetContainerId((qcContainerId >= 0) ? qcContainerId : 0);
    clickPacket.SetStateId(inv.getStateId());

    if (phase == 0 || phase == 2) {
        // 开始/结束：slotNum 为 -999（表示点击在背包外）
        clickPacket.SetSlotNum(-999);
    } else {
        // 拖过槽位：使用实际槽位号
        clickPacket.SetSlotNum((short)slotNum);
    }

    clickPacket.SetButtonNum((char)buttonNum);
    clickPacket.SetClickType(5);  // QUICK_CRAFT = 5

    // ChangedSlots: 拖拽操作由服务器处理，客户端只发送状态
    std::map<short, ProtocolCraft::Slot> changed;
    clickPacket.SetChangedSlots(changed);

    // 光标物品
    InvSlot cursor = inv.getCursorItem();
    ProtocolCraft::Slot carriedSlot;
    if (cursor.present && cursor.itemId > 0) {
        carriedSlot.SetPresent(true);
        carriedSlot.SetItemId(cursor.itemId);
        carriedSlot.SetItemCount(cursor.count);
    }
    clickPacket.SetCarriedItem(carriedSlot);

    ProtocolCraft::WriteContainer writeData;
    clickPacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));

    LOGI("sendContainerQuickCraft: phase=%d, slot=%d, button=%d, buttonNum=%d",
         phase, slotNum, button, buttonNum);
}

void ClientEngine::disconnect() {
    std::lock_guard<std::mutex> lock(netMutex);
    if (net) {
        net->disconnect();
    }
    // 清理所有实体
    m_entityManager->removeAllEntities();
    m_entityRenderer->clearTextureCache();
}

float ClientEngine::getSkyDarken() const {
    return Light::getInstance().getSkyDarken();
}

long long ClientEngine::getWorldDayTime() const {
    return Light::getInstance().getWorldDayTime();
}

void ClientEngine::loadLanguage(const std::string& json) {
    try {
        auto root = njson::parse(json);
        if (!root.is_object()) {
            LOGE("Language file is not a JSON object");
            return;
        }
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (it.value().is_string()) {
                translations[it.key()] = it.value().get<std::string>();
            }
        }
        LOGI("Loaded %zu translations", translations.size());
    } catch (const std::exception& e) {
        LOGE("Failed to parse language file: %s", e.what());
    } catch (...) {
        LOGE("Failed to parse language file: unknown error");
    }
}

std::string ClientEngine::parseChatComponent(const std::string& raw) const {
    try {
        auto j = njson::parse(raw, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return raw;

        // 纯文本类型：{"text": "..."}
        if (j.contains("text") && j["text"].is_string() && !j.contains("translate")) {
            return j["text"].get<std::string>();
        }

        // 翻译类型：{"translate": "key", "with": [...]}
        if (!j.contains("translate") || !j["translate"].is_string()) return raw;

        std::string translateKey = j["translate"].get<std::string>();
        auto it = translations.find(translateKey);
        if (it == translations.end()) {
            // 无翻译，尝试 text 字段作为回退
            return j.contains("text") && j["text"].is_string()
                ? j["text"].get<std::string>() : raw;
        }

        std::string result = it->second;

        // 解析 with 数组中的参数
        std::vector<std::string> args;
        if (j.contains("with") && j["with"].is_array()) {
            for (const auto& elem : j["with"]) {
                if (elem.contains("text") && elem["text"].is_string()) {
                    args.push_back(elem["text"].get<std::string>());
                } else if (elem.contains("translate") && elem["translate"].is_string()) {
                    std::string subKey = elem["translate"].get<std::string>();
                    auto subIt = translations.find(subKey);
                    args.push_back(subIt != translations.end() ? subIt->second : subKey);
                } else if (elem.is_string()) {
                    args.push_back(elem.get<std::string>());
                } else {
                    args.push_back("");
                }
            }
        }

        // 替换 %1$s, %2$s ...（带位置编号）
        for (size_t i = 0; i < args.size(); i++) {
            std::string placeholder = "%" + std::to_string(i + 1) + "$s";
            size_t pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                result.replace(pos, placeholder.length(), args[i]);
                pos += args[i].length();
            }
        }

        // 替换 %s（Java 非位置格式，按顺序匹配）
        size_t argIdx = 0;
        size_t pos = 0;
        while ((pos = result.find("%s", pos)) != std::string::npos && argIdx < args.size()) {
            result.replace(pos, 2, args[argIdx]);
            pos += args[argIdx].length();
            argIdx++;
        }

        return result;
    } catch (...) {
        return raw;
    }
}

