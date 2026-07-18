#include "ClientEngine.h"
#include "NetworkManager.h"
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
#include "protocolCraft/Packets/Game/Serverbound/ServerboundPlayerActionPacket.hpp"
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
// 实体相关包
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
#include "PlayerInventory.h"
#include "gui/GameUI.h"

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <map>
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

ClientEngine::ClientEngine() : chunkManager(nullptr) {
    instance = this;
}

ClientEngine::~ClientEngine() = default;

void ClientEngine::setAuthInfo(const std::string& accessToken, const std::string& uuid, const std::string& tokenType) {
    g_accessToken = accessToken;
    g_playerUuid = uuid;
    g_tokenType = tokenType;
    g_isPremium = !accessToken.empty();
    LOGI("Auth info set: premium=%d, uuid=%s", g_isPremium, g_playerUuid.c_str());
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

        if (pid == 0x02) {
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

            break;
        } else if (pid == 0x01) {
            // ========== Encryption Request (Hello) — 在线模式服务器 ==========
            LOGI("Received Encryption Request (online mode server)");

            if (!g_isPremium) {
                LOGE("Server is in online mode, but no premium auth available");
                net->disconnect();
                return false;
            }

            std::vector<unsigned char> packetData(resp.begin() + pos, resp.end());
            auto dataIter = packetData.cbegin();
            size_t dataLen = packetData.size();

            // 解析 Hello 包：serverID (String), publicKey (ByteArray), verifyToken (ByteArray)
            try {
                // 读取 server ID
                int serverIdLen = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(dataIter, dataLen);
                std::string serverID(serverIdLen, '\0');
                for (int i = 0; i < serverIdLen; ++i) {
                    serverID[i] = ProtocolCraft::ReadData<char>(dataIter, dataLen);
                }

                // 读取 public key
                int pubKeyLen = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(dataIter, dataLen);
                std::vector<unsigned char> publicKey(pubKeyLen);
                for (int i = 0; i < pubKeyLen; ++i) {
                    publicKey[i] = static_cast<unsigned char>(ProtocolCraft::ReadData<char>(dataIter, dataLen));
                }

                // 读取 verify token
                int verifyTokenLen = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(dataIter, dataLen);
                std::vector<unsigned char> verifyToken(verifyTokenLen);
                for (int i = 0; i < verifyTokenLen; ++i) {
                    verifyToken[i] = static_cast<unsigned char>(ProtocolCraft::ReadData<char>(dataIter, dataLen));
                }

                LOGI("Encryption Request: serverID_len=%d, pubKey_len=%d, verifyToken_len=%d",
                     serverIdLen, pubKeyLen, verifyTokenLen);

                // 调用 Java 层处理完整加密请求：
                // Java 负责：生成共享密钥 + SHA1 哈希 + Session Join + RSA 加密
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

                // 用共享密钥初始化 AES-128-CFB8 流加密
                aesEncrypter = std::make_unique<AESEncrypter>();
                aesEncrypter->Init(rawSharedSecret);

                if (!aesEncrypter->isInitialized()) {
                    LOGE("Failed to initialize AESEncrypter");
                    net->disconnect();
                    return false;
                }

                // 发送 Key 包 (Encryption Key Response, packet ID 0x01)
                // 格式：VarInt(0x01) + ByteArray(encrypted_shared_secret) + ByteArray(encrypted_verify_token)
                ProtocolCraft::WriteContainer keyPacket;
                ProtocolCraft::WriteData<int, ProtocolCraft::VarInt>(0x01, keyPacket);  // packet ID
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

                // 启用 AES 流加密
                net->setEncrypter(aesEncrypter.get());
                LOGI("AES-128-CFB8 stream encryption enabled on NetworkManager");

                continue;  // 继续等待 Login Success

            } catch (const std::exception& e) {
                LOGE("Failed to parse encryption request: %s", e.what());
                net->disconnect();
                return false;
            }
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
            net->disconnect();
            return false;
        } else {
            LOGE("Unexpected login packet: %d", pid);
            net->disconnect();
            return false;
        }
    }

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

    // 启动区块异步加载线程和数据包处理线程
    chunkWorkerRunning = true;
    chunkWorker = std::thread(&ClientEngine::chunkWorkerFunc, this);
    urgentProcessorRunning = true;
    urgentProcessor = std::thread(&ClientEngine::urgentProcessorFunc, this);
    normalProcessorRunning = true;
    normalProcessor = std::thread(&ClientEngine::normalProcessorFunc, this);

    // ========== PLAY 状态主循环（网络线程仅做 I/O + 按优先级入队） ==========
    while (true) {
        auto resp = net->receivePacket();
        if (resp.empty()) {
            LOGI("Connection closed");
            break;
        }

        // 网络线程只做最轻量的 VarInt 解析（读取 1-3 字节），随后按优先级入队
        size_t pos = 0;
        ProtocolCraft::ReadIterator iter = resp.cbegin();
        size_t remaining = resp.size();
        int pid = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, remaining);
        pos = resp.size() - remaining;

        // 收到第一个 PLAY 状态包后，发送 ClientInformation
        // 这确保服务器已经完全切换到 PLAY 状态
        if (!clientInfoSent) {
            clientInfoSent = true;
            
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
            
            // 注意：这里需要锁住 netMutex 来发送
            {
                std::lock_guard<std::mutex> lock(netMutex);
                if (net) {
                    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
                    LOGI("Sent Client Information (ViewDistance=10)");
                }
            }
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

    // 停止紧急数据包处理线程
    urgentProcessorRunning = false;
    urgentCV.notify_all();
    if (urgentProcessor.joinable()) {
        urgentProcessor.join();
    }

    // 停止普通数据包处理线程
    normalProcessorRunning = false;
    normalCV.notify_all();
    if (normalProcessor.joinable()) {
        normalProcessor.join();
    }

    // 停止区块加载线程
    chunkWorkerRunning = false;
    chunkCV.notify_all();
    if (chunkWorker.joinable()) {
        chunkWorker.join();
    }

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

void ClientEngine::sendContainerClick(int slotNum, int button, int containerId) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    // 确定实际容器 ID
    if (containerId < 0) {
        int openId = GameUI::getInstance().getOpenContainerId();
        containerId = (openId >= 0) ? openId : 0;
    }

    auto& inv = PlayerInventory::getInstance();
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

    auto& inv = PlayerInventory::getInstance();

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
    EntityManager::getInstance().removeAllEntities();
    EntityRenderer::getInstance().clearTextureCache();
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
                } else {
                    args.push_back("");
                }
            }
        }

        // 替换 %1$s, %2$s ...
        for (size_t i = 0; i < args.size(); i++) {
            std::string placeholder = "%" + std::to_string(i + 1) + "$s";
            size_t pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                result.replace(pos, placeholder.length(), args[i]);
                pos += args[i].length();
            }
        }

        return result;
    } catch (...) {
        return raw;
    }
}

void ClientEngine::chunkWorkerFunc() {
    LOGI("Chunk worker thread started");
    while (chunkWorkerRunning) {
        std::unique_lock<std::mutex> lock(chunkQueueMutex);
        chunkCV.wait_for(lock, std::chrono::milliseconds(100), [this]() {
            return !chunkQueue.empty() || !chunkWorkerRunning;
        });

        if (chunkQueue.empty() || !chunkWorkerRunning) {
            lock.unlock();
            continue;
        }

        auto task = std::move(chunkQueue.front());
        chunkQueue.pop();
        lock.unlock();

        // 处理这一个区块（ProtocolCraft 解析 + loadChunk + 标记更新）
        ProtocolCraft::ClientboundLevelChunkWithLightPacket chunkPacket;
        auto iter = task.rawData.cbegin();
        size_t len = task.rawData.size();

        try {
            chunkPacket.Read(iter, len);
        } catch (const std::exception& e) {
            LOGE("Chunk worker: failed to parse chunk: %s", e.what());
            continue;
        }

        int chunkX = chunkPacket.GetX();
        int chunkZ = chunkPacket.GetZ();

        auto rawIter = task.rawData.cbegin() + 8;
        long long bitMask = 0;
        for (int i = 0; i < 8; i++) {
            bitMask = (bitMask << 8) | *rawIter;
            ++rawIter;
        }

        const auto& buffer_data = chunkPacket.GetChunkData().GetBuffer();
        if (buffer_data.empty()) continue;

        std::vector<uint8_t> rawData(buffer_data.begin(), buffer_data.end());
        std::vector<uint8_t> emptyHeightmaps;
        std::vector<uint8_t> emptyBlockEntities;

        try {
            chunkManager->loadChunk(chunkX, chunkZ, rawData, true, bitMask,
                                    emptyHeightmaps, emptyBlockEntities, dimensionMinY);

            // ===== 提取光照数据 =====
            auto chunk = chunkManager->getChunk(chunkX, chunkZ);
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
                    auto& section = chunk->sections[i];
                    if (!section) continue;

                    // Light section bits are offset by 1 from chunk section indices
                    // bit 0 = Y=-80 (padding), bit 1 = Y=-64 (chunk section 0), etc.
                    int lightBit = i + 1;
                    if (skyMask & (1ULL << lightBit)) {
                        if (skyIdx < (int)skyUpdates.size()) {
                            const auto& data = skyUpdates[skyIdx];
                            section->skyLight.resize(2048);
                            memcpy(section->skyLight.data(), data.data(),
                                   std::min(data.size(), (size_t)2048));
                        }
                        skyIdx++;
                    } else if (emptySkyMask & (1ULL << lightBit)) {
                        section->skyLight.assign(2048, 0);
                    }

                    if (blockMask & (1ULL << lightBit)) {
                        if (blockIdx < (int)blockUpdates.size()) {
                            const auto& data = blockUpdates[blockIdx];
                            section->blockLight.resize(2048);
                            memcpy(section->blockLight.data(), data.data(),
                                   std::min(data.size(), (size_t)2048));
                        }
                        blockIdx++;
                    } else if (emptyBlockMask & (1ULL << lightBit)) {
                        section->blockLight.assign(2048, 0);
                    }
                }
            }

            if (glRenderer) {
                glRenderer->setChunkManager(chunkManager.get());
                glRenderer->markChunkForUpdate(chunkX, chunkZ);
            }
        } catch (const std::exception& e) {
            LOGE("Chunk worker: failed to load chunk (%d,%d): %s", chunkX, chunkZ, e.what());
        }
    }
    LOGI("Chunk worker thread stopped");
}

void ClientEngine::urgentProcessorFunc() {
    LOGI("Urgent packet processor thread started");
    while (urgentProcessorRunning) {
        PacketTask task;
        {
            std::unique_lock<std::mutex> lock(urgentQueueMutex);
            urgentCV.wait(lock, [this]() {
                return !urgentQueue.empty() || !urgentProcessorRunning;
            });
            if (!urgentProcessorRunning && urgentQueue.empty()) break;
            task = std::move(urgentQueue.front());
            urgentQueue.pop();
        }
        handlePlayPacket(task.packetId, task.data, task.startPos);
    }
    while (true) {
        PacketTask task;
        {
            std::lock_guard<std::mutex> lock(urgentQueueMutex);
            if (urgentQueue.empty()) break;
            task = std::move(urgentQueue.front());
            urgentQueue.pop();
        }
        handlePlayPacket(task.packetId, task.data, task.startPos);
    }
    LOGI("Urgent packet processor thread stopped");
}

void ClientEngine::normalProcessorFunc() {
    LOGI("Normal packet processor thread started");
    while (normalProcessorRunning) {
        PacketTask task;
        {
            std::unique_lock<std::mutex> lock(normalQueueMutex);
            normalCV.wait(lock, [this]() {
                return !normalQueue.empty() || !normalProcessorRunning;
            });
            if (!normalProcessorRunning && normalQueue.empty()) break;
            task = std::move(normalQueue.front());
            normalQueue.pop();
        }
        handlePlayPacket(task.packetId, task.data, task.startPos);
    }
    while (true) {
        PacketTask task;
        {
            std::lock_guard<std::mutex> lock(normalQueueMutex);
            if (normalQueue.empty()) break;
            task = std::move(normalQueue.front());
            normalQueue.pop();
        }
        handlePlayPacket(task.packetId, task.data, task.startPos);
    }
    LOGI("Normal packet processor thread stopped");
}

void ClientEngine::handlePlayPacket(int packetId,
                                    const std::vector<uint8_t>& data, size_t startPos) {
    try {
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
                    LOGI("Player ID: %d, GameType: %d",
                         loginPacket.GetPlayerId(),
                         gameMode);

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

                                        // 记录服务器 ID → 名称的映射
                                        if (idIt != entry.end() && idIt->second.is<int>()) {
                                            serverIdToName[idIt->second.get<int>()] = biomeName;
                                        }

                                        BiomeColorManager::BiomeEntry biomeEntry;

                                        // temperature / downfall
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

                                            // 解析 effects
                                            auto effectsIt = elemCompound.find("effects");
                                            if (effectsIt != elemCompound.end() && effectsIt->second.is<ProtocolCraft::NBT::TagCompound>()) {
                                                const auto& effects = effectsIt->second.get<ProtocolCraft::NBT::TagCompound>();

                                                // 固定草颜色
                                                auto grassColorIt = effects.find("grass_color");
                                                if (grassColorIt != effects.end() && grassColorIt->second.is<int>()) {
                                                    int color = grassColorIt->second.get<int>();
                                                    biomeEntry.hasFixedGrassColor = true;
                                                    biomeEntry.fixedGrassR = (color >> 16) & 0xFF;
                                                    biomeEntry.fixedGrassG = (color >> 8) & 0xFF;
                                                    biomeEntry.fixedGrassB = color & 0xFF;
                                                }

                                                // 固定树叶颜色
                                                auto foliageColorIt = effects.find("foliage_color");
                                                if (foliageColorIt != effects.end() && foliageColorIt->second.is<int>()) {
                                                    int color = foliageColorIt->second.get<int>();
                                                    biomeEntry.hasFixedFoliageColor = true;
                                                    biomeEntry.fixedFoliageR = (color >> 16) & 0xFF;
                                                    biomeEntry.fixedFoliageG = (color >> 8) & 0xFF;
                                                    biomeEntry.fixedFoliageB = color & 0xFF;
                                                }

                                                // 固定水颜色
                                                auto waterColorIt = effects.find("water_color");
                                                if (waterColorIt != effects.end() && waterColorIt->second.is<int>()) {
                                                    int color = waterColorIt->second.get<int>();
                                                    biomeEntry.hasFixedWaterColor = true;
                                                    biomeEntry.fixedWaterR = (color >> 16) & 0xFF;
                                                    biomeEntry.fixedWaterG = (color >> 8) & 0xFF;
                                                    biomeEntry.fixedWaterB = color & 0xFF;
                                                }

                                                // grass_color_modifier（沼泽、黑森林等）
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
                                        LOGI("Applying server biome registry mapping, %zu entries",
                                             serverBiomes.size());
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
                    // Minecraft 1.18+ 每个维度有自己的 min_y 和 height
                    // 主世界: min_y=-64, height=384  |  下界/末地: min_y=0, height=256
#if PROTOCOL_VERSION < 759
                    // 1.18.2: DimensionType 是 NBT 复合标签，直接包含 min_y 和 height
                    try {
                        const auto& dimType = loginPacket.GetDimensionType();
                        if (dimType.contains("min_y") && dimType.contains("height")) {
                            dimensionMinY = dimType["min_y"].get<int>();
                            dimensionHeight = dimType["height"].get<int>();
                            // 同步更新 VersionManager
                            VersionManager::getInstance().setDimensionConfig(dimensionMinY, dimensionMinY + dimensionHeight);
                        }
                    } catch (const std::exception& e) {
                        LOGW("Failed to parse DimensionType from Login: %s", e.what());
                        dimensionMinY = -64;
                        dimensionHeight = 384;
                        VersionManager::getInstance().setDimensionConfig(-64, 320);
                    }
#else
                    // 1.19+: DimensionType 是 Identifier，从 RegistryHolder 解析
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
            case 0x21:
#else
            case 0x23:
#endif
            { // Keep Alive (Clientbound)
                if (data.size() - startPos >= 8) {
                    // 手动解析 8 字节大端序 Long（ProtocolCraft 有 bug）
                    long long keepAliveId = 0;
                    for (int i = 0; i < 8; i++) {
                        keepAliveId = (keepAliveId << 8) | data[startPos + i];
                    }

                    // 手动构造响应包：Packet ID (VarInt) + KeepAlive ID (8 bytes Big Endian)
                    std::vector<uint8_t> response;

                    // Packet ID: Serverbound KeepAlive
#if PROTOCOL_VERSION >= 762
                    response.push_back(0x12);
#else
                    response.push_back(0x0F);
#endif

                    // KeepAlive ID: 8 bytes Big Endian
                    for (int i = 7; i >= 0; i--) {
                        response.push_back((keepAliveId >> (i * 8)) & 0xFF);
                    }

                    bool sent = sendPacket(response);
                    if (!sent) {
                        LOGE("Failed to send KeepAlive response!");
                    }
                }
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x38:
#else
            case 0x3C:
#endif
            { // Player Position And Look
                ProtocolCraft::ClientboundPlayerPositionPacket posPacket;
                std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
                auto iter = packetData.cbegin();
                size_t length = packetData.size();

                posPacket.Read(iter, length);

                // 保存玩家位置
                playerX = posPacket.GetX();
                playerY = posPacket.GetY();
                playerZ = posPacket.GetZ();
                yaw = glm::radians(posPacket.GetYRot());
                pitch = glm::radians(posPacket.GetXRot());
                hasPosition = true;

                {
                    glm::vec3 curPos = Collision::getInstance().getPosition();
                    glm::vec3 curVel = Collision::getInstance().getVelocity();
                    float diffX = curPos.x - playerX;
                    float diffY = curPos.y - playerY;
                    float diffZ = curPos.z - playerZ;
                    float dist = sqrtf(diffX * diffX + diffY * diffY + diffZ * diffZ);
                    LOGI("Received teleport request, ID=%d, server=(%.3f, %.3f, %.3f), client=(%.3f, %.3f, %.3f), dist=%.3f",
                         posPacket.GetId_(),
                         playerX, playerY, playerZ,
                         curPos.x, curPos.y, curPos.z,
                         dist);
                }

                // 直接同步摄像机位置和碰撞系统位置（每次收到都更新，支持传送、重生等）
                CameraController::getInstance().setPosition(playerX, playerY, playerZ);
                CameraController::getInstance().setRotation(pitch, yaw);
                Collision::getInstance().setPosition(playerX, playerY, playerZ);

                // 同步 lastSent 缓存，防止渲染线程发送旧位置再次触发回弹
                lastSent.x = playerX;
                lastSent.y = playerY;
                lastSent.z = playerZ;
                lastSent.yaw = yaw;
                lastSent.pitch = pitch;
                lastSent.onGround = true;
                lastSent.initialized = true;

                LOGI("Camera synced to player position");

                // 发送 Teleport Confirm
                ProtocolCraft::ServerboundAcceptTeleportationPacket confirmPacket;
                confirmPacket.SetId_(posPacket.GetId_());

                ProtocolCraft::WriteContainer writeData;
                confirmPacket.Write(writeData);

                LOGI("Sending TeleportConfirm with ID=%d", posPacket.GetId_());
                sendPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));

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
                sendPacket(std::vector<uint8_t>(moveData.begin(), moveData.end()));

                // 首次收到坐标后，才允许渲染线程发送移动包
                movementEnabled = true;
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x22:
#else
            case 0x24:
#endif
            { // Chunk Data (Level Chunk with Light) — 入队异步处理，不阻塞网络循环
                {
                    std::lock_guard<std::mutex> lock(chunkQueueMutex);
                    chunkQueue.push({std::vector<uint8_t>(data.begin() + startPos, data.end())});
                }
                chunkCV.notify_one();
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x0C:
#else
            case 0x0A:
#endif
            { // Block Update（单一方块更新）
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

                // 区块坐标转换（支持负数）
                int chunkX = blockX >> 4;
                int chunkZ = blockZ >> 4;
                int localX = blockX & 15;
                int localZ = blockZ & 15;

                if (chunkManager) {
                    auto chunk = chunkManager->getChunk(chunkX, chunkZ);
                    if (chunk) {
                        chunk->setBlockState(localX, blockY, localZ, blockState);
                        // 异步入队方块光重算（不阻塞网络线程）
                        Light::getInstance().queueBlockLightRecalc(blockX, blockY, blockZ);
                        if (glRenderer) {
                            glRenderer->markChunkForUpdate(chunkX, chunkZ);
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
                    if (chunkManager) {
                        auto chunk = chunkManager->getChunk(lx, lz);
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
                                // Light section bits are offset by 1 from chunk section indices
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
                            if (glRenderer) glRenderer->markChunkForUpdate(lx, lz);
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
            { // Section Blocks Update（多方块批量更新）
                ProtocolCraft::ClientboundSectionBlocksUpdatePacket sectionPacket;
                std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
                auto iter = packetData.cbegin();
                size_t length = packetData.size();
                sectionPacket.Read(iter, length);

                long long sectionPos = sectionPacket.GetSectionPos();

                // 解码 section position（SectionPosition 编码：X(22) << 42 | Z(22) << 20 | Y(20)）
                // 必须用无符号算术避免符号位传播导致解码错误
                uint64_t rawPos = (uint64_t)sectionPos;
                int chunkX = (int)((rawPos >> 42) & 0x3FFFFF);
                if (chunkX >= 2097152) chunkX -= 4194304;
                int chunkZ = (int)((rawPos >> 20) & 0x3FFFFF);
                if (chunkZ >= 2097152) chunkZ -= 4194304;
                int sectionY = (int)(rawPos & 0xFFFFF);
                if (sectionY >= 524288) sectionY -= 1048576;

                if (!chunkManager) break;
                auto chunk = chunkManager->getChunk(chunkX, chunkZ);
                if (!chunk) {
                    LOGE("SectionBlocksUpdate: chunk (%d, %d) not loaded", chunkX, chunkZ);
                    break;
                }

                const auto& posState = sectionPacket.GetPosState();
                for (const auto& entry : posState) {
                    uint64_t entryVal = (uint64_t)entry;
                    int sectionLocalIndex = (int)(entryVal & 0xFFF);
                    int blockState = (int)(entryVal >> 12);

                    int localX = (sectionLocalIndex >> 8) & 0xF;   // bits 8-11
                    int localZ = (sectionLocalIndex >> 4) & 0xF;   // bits 4-7
                    int localY = sectionLocalIndex & 0xF;           // bits 0-3

                    int blockY = sectionY * 16 + localY;
                    chunk->setBlockState(localX, blockY, localZ, blockState);
                }

                if (glRenderer) {
                    glRenderer->markChunkForUpdate(chunkX, chunkZ);
                }
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x14:
#else
            case 0x12:
#endif
            { // Container Set Content（设置容器全部物品）
                ProtocolCraft::ClientboundContainerSetContentPacket containerPacket;
                std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
                auto iter = packetData.cbegin();
                size_t length = packetData.size();
                containerPacket.Read(iter, length);

                int containerId = containerPacket.GetContainerId();
                const auto& items = containerPacket.GetItems();

                std::vector<InvSlot> invSlots;
                invSlots.reserve(items.size());
                int nonEmptyCount = 0;
                for (const auto& slot : items) {
                    InvSlot is;
                    is.present = !slot.IsEmptySlot();
                    if (is.present) {
                        is.itemId = slot.GetItemId();
                        is.count = slot.GetItemCount();
                        nonEmptyCount++;
                    }
                    invSlots.push_back(is);
                }

                PlayerInventory::getInstance().setContent(containerId, invSlots);
                PlayerInventory::getInstance().setStateId(containerPacket.GetStateId());

                // 更新光标物品
                const auto& carried = containerPacket.GetCarriedItem();
                InvSlot cursorIs;
                cursorIs.present = !carried.IsEmptySlot();
                if (cursorIs.present) {
                    cursorIs.itemId = carried.GetItemId();
                    cursorIs.count = carried.GetItemCount();
                }
                PlayerInventory::getInstance().setCursorItem(cursorIs);

                LOGI("Container Set Content: id=%d, state=%d, slots=%zu, nonEmpty=%d",
                     containerId, containerPacket.GetStateId(), items.size(), nonEmptyCount);
                // 日志：前 3 个非空物品
                int logged = 0;
                for (size_t i = 0; i < items.size() && logged < 3; i++) {
                    if (invSlots[i].present) {
                        LOGI("  slot[%zu]: itemId=%d, count=%d",
                             i, invSlots[i].itemId, invSlots[i].count);
                        logged++;
                    }
                }
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x16:
#else
            case 0x14:
#endif
            { // Container Set Slot（设置单个格子）
                ProtocolCraft::ClientboundContainerSetSlotPacket slotPacket;
                std::vector<unsigned char> packetData(data.begin() + startPos, data.end());
                auto iter = packetData.cbegin();
                size_t length = packetData.size();
                slotPacket.Read(iter, length);

                int containerId = slotPacket.GetContainerId();
                int slotIndex = slotPacket.GetSlot();
                const auto& pcSlot = slotPacket.GetItemStack();

                InvSlot is;
                is.present = !pcSlot.IsEmptySlot();
                if (is.present) {
                    is.itemId = pcSlot.GetItemId();
                    is.count = pcSlot.GetItemCount();
                }

                PlayerInventory::getInstance().setSlot(containerId, slotIndex, is);
                PlayerInventory::getInstance().setStateId(slotPacket.GetStateId());
                LOGI("Container Set Slot: id=%d, slot=%d, present=%d, itemId=%d, count=%d",
                     containerId, slotIndex, is.present, is.itemId, is.count);
                break;
            }
#if PROTOCOL_VERSION < 762
            case 0x35:
#else
            case 0x38:
#endif
            { // Combat Kill (death message)
                ProtocolCraft::ClientboundPlayerCombatKillPacket killPacket;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                killPacket.Read(iter, len);
                deathMessage = killPacket.GetMessage().GetText();
                if (deathMessage.empty()) {
                    deathMessage = parseChatComponent(killPacket.GetMessage().GetRawText());
                }
                LOGI("Death message: '%s'", deathMessage.c_str());
                break;
            }


#if PROTOCOL_VERSION < 762
            case 0x51:
#else
            case 0x56:
#endif
            { // Set Experience（经验值更新）
                ProtocolCraft::ClientboundSetExperiencePacket expPacket;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                expPacket.Read(iter, len);
                experienceProgress = expPacket.GetExperienceProgress();
                experienceLevel = expPacket.GetExperienceLevel();
                totalExperience = expPacket.GetTotalExperience();
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x52:
#else
            case 0x57:
#endif
            { // Set Health（玩家生命/饥饿值更新）
                ProtocolCraft::ClientboundSetHealthPacket healthPacket;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                healthPacket.Read(iter, len);
                health = healthPacket.GetHealth();
                food = healthPacket.GetFood();
                foodSaturation = healthPacket.GetFoodSaturation();
                LOGI("Health: %.1f, Food: %d, Saturation: %.1f",
                     health, food, foodSaturation);
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x1E:
#else
            case 0x1F:
#endif
            { // Game Event（包含游戏模式变更）
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
            { // Respawn（维度切换/重生）
                ProtocolCraft::ClientboundRespawnPacket respawnPacket;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                respawnPacket.Read(iter, len);
                int newMode = respawnPacket.GetPlayerGameType();
                gameMode = newMode;
                Collision::getInstance().setGameMode(newMode);

                // 从 Respawn 包解析新维度的 min_y / height
#if PROTOCOL_VERSION < 759
                // 1.18.2: DimensionType 是 NBT 复合标签
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
                // 1.19+: DimensionType 是 Identifier，Respawn 包不含 RegistryHolder
                // 维度参数已在 Login 时从 RegistryHolder 解析并缓存
                LOGI("Respawn: dimension changed to %s",
                     respawnPacket.GetDimensionType().GetFull().c_str());
#endif

                // 维度切换：清理旧维度的区块和实体
                // 服务器随后会发送新维度的 ChunkData 和 TeleportEntity 包
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

#if PROTOCOL_VERSION < 762
            case 0x59:
#else
            case 0x5E:
#endif
            { // Set Time（昼夜时间更新）
                ProtocolCraft::ClientboundSetTimePacket timePacket;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                timePacket.Read(iter, len);
                Light::getInstance().setWorldDayTime(timePacket.GetDayTime());
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x48:
#else
            case 0x4D:
#endif
            { // Set Carried Item（服务器同步手持槽位）
                ProtocolCraft::ClientboundSetCarriedItemPacket carriedPacket;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                carriedPacket.Read(iter, len);
                int slot = (int)carriedPacket.GetSlot();
                if (slot >= 0 && slot <= 8) {
                    PlayerInventory::getInstance().setSelectedSlot(slot);
                }
                break;
            }

            // ===== 实体包处理 =====
#if PROTOCOL_VERSION >= 762
            case 0x00: { // BundlePacket（包捆绑标记，跳过）
                break;
            }
#endif

#if PROTOCOL_VERSION < 762
            case 0x00:
#else
            case 0x01:
#endif
            { // Spawn Entity（通用实体生成）
                // 1.19.4+: 服务器可能发送 BundleMarker（空包体 0x00）
                if (data.size() <= startPos + 1) {
                    // Body 为空的 0x00 包 = BundleMarker（包捆绑标记），跳过
                    break;
                }
                LOGI("AddEntity: data.size=%zu, startPos=%zu, bodySize=%zu",
                     data.size(), startPos, (data.size() > startPos) ? (data.size() - startPos) : 0);
                if (data.size() <= startPos) {
                    LOGE("AddEntity: packet data too short, skipping");
                    break;
                }
                ProtocolCraft::ClientboundAddEntityPacket pkt;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                pkt.Read(iter, len);
                Entity e;
                e.entityId = pkt.GetEntityId();
                e.protocolTypeId = pkt.GetType();
                e.type = entityTypeFromProtocolId(pkt.GetType());
                LOGI("SpawnEntity(0x00): entityId=%d, typeId=%d, type=%s",
                     e.entityId, e.protocolTypeId, e.getTypeName());
                e.x = pkt.GetX();
                e.y = pkt.GetY();
                e.z = pkt.GetZ();
                e.yaw = pkt.GetYRot() * 360.0f / 256.0f;
                e.pitch = pkt.GetXRot() * 360.0f / 256.0f;
                e.headYaw = e.yaw;
                EntityManager::getInstance().addEntity(e);
                break;
            }

#if PROTOCOL_VERSION < 759
            case 0x02: { // Spawn Mob（生物生成：僵尸、动物等，1.18.2 专用）
                ProtocolCraft::ClientboundAddMobPacket pkt;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                pkt.Read(iter, len);
                Entity e;
                e.entityId = pkt.GetEntityId();
                e.protocolTypeId = pkt.GetType();
                e.type = entityTypeFromProtocolId(pkt.GetType());
                e.x = pkt.GetX();
                e.y = pkt.GetY();
                e.z = pkt.GetZ();
                e.yaw = pkt.GetYRot() * 360.0f / 256.0f;
                e.pitch = pkt.GetXRot() * 360.0f / 256.0f;
                e.headYaw = pkt.GetYHeadRot() * 360.0f / 256.0f;
                LOGI("SpawnMob: entityId=%d, typeId=%d, entityType=%s",
                     e.entityId, e.protocolTypeId, e.getTypeName());
                EntityManager::getInstance().addEntity(e);
                break;
            }
#endif

#if PROTOCOL_VERSION < 764
#if PROTOCOL_VERSION < 762
            case 0x04:
#else
            case 0x03:
#endif
            { // Spawn Player（玩家生成，1.20.1 及以下）
                ProtocolCraft::ClientboundAddPlayerPacket pkt;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                pkt.Read(iter, len);
                Entity e;
                e.entityId = pkt.GetEntityId();
                e.type = EntityType::PLAYER;
                e.x = pkt.GetX();
                e.y = pkt.GetY();
                e.z = pkt.GetZ();
                e.yaw = pkt.GetYRot() * 360.0f / 256.0f;
                e.pitch = pkt.GetXRot() * 360.0f / 256.0f;
                e.headYaw = e.yaw;
                EntityManager::getInstance().addEntity(e);
                break;
            }
#endif

#if PROTOCOL_VERSION < 762
            case 0x2A:
#else
            case 0x2C:
#endif
            { // Entity Position and Rotation（相对移动+旋转）
                ProtocolCraft::ClientboundMoveEntityPacketPosRot pkt;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                pkt.Read(iter, len);
                float yaw = pkt.GetYRot() * 360.0f / 256.0f;
                float pitch = pkt.GetXRot() * 360.0f / 256.0f;
                EntityManager::getInstance().moveEntityRot(
                    pkt.GetEntityId(), pkt.GetXA(), pkt.GetYA(), pkt.GetZA(),
                    yaw, pitch);
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x29:
#else
            case 0x2B:
#endif
            { // Entity Position（相对移动，无旋转）
                ProtocolCraft::ClientboundMoveEntityPacketPos pkt;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                pkt.Read(iter, len);
                EntityManager::getInstance().moveEntity(
                    pkt.GetEntityId(), pkt.GetXA(), pkt.GetYA(), pkt.GetZA());
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x2B:
#else
            case 0x2D:
#endif
            { // Entity Rotation（仅旋转）
                ProtocolCraft::ClientboundMoveEntityPacketRot pkt;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                pkt.Read(iter, len);
                float yaw = pkt.GetYRot() * 360.0f / 256.0f;
                float pitch = pkt.GetXRot() * 360.0f / 256.0f;
                EntityManager::getInstance().rotateEntity(pkt.GetEntityId(), yaw, pitch);
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x62:
#else
            case 0x68:
#endif
            { // Teleport Entity（绝对传送）
                ProtocolCraft::ClientboundTeleportEntityPacket pkt;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                pkt.Read(iter, len);
                float yaw = pkt.GetYRot() * 360.0f / 256.0f;
                float pitch = pkt.GetXRot() * 360.0f / 256.0f;
                EntityManager::getInstance().teleportEntity(
                    pkt.GetEntityId(), pkt.GetX(), pkt.GetY(), pkt.GetZ(),
                    yaw, pitch);
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x3A:
#else
            case 0x3E:
#endif
            { // Remove Entities（移除实体）
                ProtocolCraft::ClientboundRemoveEntitiesPacket pkt;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                pkt.Read(iter, len);
                const auto& ids = pkt.GetEntityIds();
                for (auto id : ids) {
                    EntityManager::getInstance().removeEntity((int)id);
                }
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x4F:
#else
            case 0x54:
#endif
            { // Set Entity Motion（设置速度）
                ProtocolCraft::ClientboundSetEntityMotionPacket pkt;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                pkt.Read(iter, len);
                EntityManager::getInstance().setEntityMotion(
                    pkt.GetEntityId(), pkt.GetXA(), pkt.GetYA(), pkt.GetZA());
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x2E:
#else
            case 0x30:
#endif
            { // Open Screen（服务器打开容器 UI）
                ProtocolCraft::ClientboundOpenScreenPacket pkt;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                pkt.Read(iter, len);
                int containerId = pkt.GetContainerId();
                int containerType = pkt.GetType();
                LOGI("OpenScreen: containerId=%d, type=%d", containerId, containerType);
                GameUI::getInstance().openContainer(containerId, containerType);
                break;
            }

#if PROTOCOL_VERSION < 762
            case 0x13:
#else
            case 0x11:
#endif
            { // Container Close（服务器关闭容器）
                GameUI::getInstance().closeContainer();
                LOGI("Container closed by server");
                break;
            }

            default: {
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