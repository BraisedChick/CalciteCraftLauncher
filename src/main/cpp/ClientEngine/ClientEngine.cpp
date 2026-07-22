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

// ProtocolCraft 澶存枃浠?
#include "protocolCraft/BinaryReadWrite.hpp"
#include "protocolCraft/Packets/Handshake/Serverbound/ServerboundClientIntentionPacket.hpp"

// ProtocolCraft 澶存枃浠?- 鐧诲綍闃舵
#include "protocolCraft/Packets/Login/Serverbound/ServerboundHelloPacket.hpp"
#include "protocolCraft/Packets/Login/Clientbound/ClientboundLoginCompressionPacket.hpp"
#include "protocolCraft/Packets/Login/Clientbound/ClientboundGameProfilePacket.hpp"
#include "protocolCraft/Packets/Login/Clientbound/ClientboundLoginDisconnectPacket.hpp"

// ProtocolCraft 澶存枃浠?- 娓告垙闃舵
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
// 鑱婂ぉ鍖?
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

// 鍓嶅悜澹版槑锛氬湪 native-lib.cpp 涓畾涔夛紝鐢ㄤ簬閫氳繃 JNI 璋冪敤 Java 灞傚鐞嗗畬鏁村姞瀵嗚姹?
// Java 灞傝礋璐ｏ細鐢熸垚鍏变韩瀵嗛挜 + SHA1 鍝堝笇 + Session Join + RSA 鍔犲瘑
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

    // 鍒濆鍖栧帇缂╃姸鎬?
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

    // ========== 鎻℃墜闃舵 ==========
    {
        LOGI("Sending handshake packet via ProtocolCraft");

        // 浠?VersionManager 鑾峰彇鍗忚鐗堟湰锛堢敱 Java 灞傜増鏈€夋嫨璁剧疆锛?
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

    // ========== 鐧诲綍闃舵 - 鍙戦€?Login Start ==========
    {
        LOGI("Sending login start: %s", username.c_str());

        ProtocolCraft::ServerboundHelloPacket loginStart;
        #if PROTOCOL_VERSION < 759
                loginStart.SetGameProfile(username);  // 1.18.2: 鐩存帴璁剧疆鐢ㄦ埛鍚?
        #else
                loginStart.SetName_(username);  // 1.19+: 璁剧疆 Name_ 瀛楁
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

    // ========== 鐧诲綍闃舵 - 鎺ユ敹鍝嶅簲 ==========
    while (true) {
        auto resp = net->receivePacket();
        if (resp.empty()) {
            LOGE("Empty response during login");
            net->disconnect();
            return false;
        }

        // 浣跨敤 VarInt 璇诲彇 Packet ID
        size_t pos = 0;
        ProtocolCraft::ReadIterator iter = resp.cbegin();
        size_t remaining = resp.size();
        int pid = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, remaining);
        pos = resp.size() - remaining;  // 鏇存柊宸茶鍙栫殑浣嶇疆

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

            goto loginDone; //鐧诲綍缁撴潫锛岀洿鎺ヨ烦鍑哄惊鐜?
        }

        case 0x01: {
            // Encryption Request (Hello) 鈥?鍦ㄧ嚎妯″紡鏈嶅姟鍣?
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
            continue;  // 缁х画绛夊緟 Login Success
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
    // 鍚敤鍙戦€佸帇缂╋紙鍙傜収 Botcraft锛氭敹鍒?Set Compression 鍚庣珛鍗冲惎鐢ㄦ敹鍙戝帇缂╋級
    // Botcraft 鐢ㄥ崟涓€ compression 鍙橀噺鎺у埗锛屾垜浠湪鐧诲綍寰幆缁撴潫鍚庣粺涓€鍚敤
    if (Compression::isReceiveEnabled()) {
        Compression::setEnabled(true);
        LOGI("Compression fully enabled (threshold=%d)", Compression::getThreshold());
    } else {
        LOGI("Compression not enabled by server (offline mode)");
    }

    // 娉ㄦ剰锛氫笉鍦ㄨ繖閲屽彂閫?ClientInformation锛?
    // 鏌愪簺鏈嶅姟鍣ㄥ湪 Login Success 涔嬪悗闇€瑕佹椂闂村垏鎹㈠埌 PLAY 鐘舵€?
    // 蹇呴』绛夋敹鍒版湇鍔″櫒鐨勭涓€涓?PLAY 鐘舵€佸寘涔嬪悗鍐嶅彂閫?
    bool clientInfoSent = false;

    // 杩涘叆 PLAY 鐘舵€侊紙娉ㄦ剰锛氫笉鍦ㄨ繖閲屽惎鐢ㄧЩ鍔ㄥ彂閫侊紝蹇呴』绛夋敹鍒扮涓€涓?0x38 纭繚鍧愭爣姝ｇ‘锛?
    // 绉诲姩鍖呯殑鍚敤鏀惧湪 handlePlayPacket 鐨?0x38 鍒嗘敮涓?

    // 濮旀墭 NetworkManager 澶勭悊 PLAY 鐘舵€佺綉缁滃惊鐜?
    net->setEngine(this);
    net->registerHandlers();
    net->startPlayLoop();  // 闃诲鐩村埌鏂紑杩炴帴

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

    // 闄愰€?20 娆?绉掞紙50ms 闂撮殧锛夛紝鍖归厤鍘熺増娓告垙鍒婚€熺巼
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
        // 浣嶇疆鎴栨棆杞彉鍖栨椂鍙戦€佸畬鏁寸Щ鍔ㄥ寘
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

    // 浣嶇疆鏈彉锛屾瘡 500ms 鍙戦€佷竴娆?StatusOnly 鍚屾鍦伴潰鐘舵€?
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

    // Action 0 = START_DIGGING (寮€濮嬫寲鎺?
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

    // Action 2 = STOP_DESTROY_BLOCK (瀹屾垚鎸栨帢)
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

    // Action 1 = ABORT_DESTROY_BLOCK (涓柇鎸栨帢)
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
    // SaltSignature 鐢ㄧ┖绛惧悕
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

    // 纭畾瀹為檯瀹瑰櫒 ID
    if (containerId < 0) {
        int openId = GameUI::getInstance().getOpenContainerId();
        containerId = (openId >= 0) ? openId : 0;
    }

    auto& inv = PlayerInventory::getInstance();
    InvSlot cursor = inv.getCursorItem();
    // 鏍规嵁瀹瑰櫒ID璇诲彇姝ｇ‘鐨勬Ы浣嶆暟缁?
    InvSlot clicked = (containerId > 0) ? inv.getContainerSlot(slotNum) : inv.getSlot(slotNum);

    // 鏋勫缓鐐瑰嚮鍚庣殑鍏夋爣鍜屾Ы浣嶇姸鎬侊紙瀹㈡埛绔娴嬶級
    InvSlot newCursor = cursor;
    InvSlot newClicked = clicked;

    if (button == 0) {  // 宸﹂敭锛氭嬁鍙?鏀句笅/浜ゆ崲
        if (!cursor.present && clicked.present) {
            // 鍏夋爣绌猴紝妲戒綅鏈夌墿鍝?鈫?鎷胯捣
            newCursor = clicked;
            newClicked = InvSlot{};
        } else if (cursor.present && !clicked.present) {
            // 鍏夋爣鏈夌墿鍝侊紝妲戒綅绌?鈫?鏀句笅
            newClicked = cursor;
            newCursor = InvSlot{};
        } else if (cursor.present && clicked.present && cursor.itemId == clicked.itemId) {
            // 鍚岀被鐗╁搧鍚堝苟
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
            // 涓嶅悓鐗╁搧 鈫?浜ゆ崲
            newCursor = clicked;
            newClicked = cursor;
        }
    } else if (button == 1) {  // 鍙抽敭锛氭斁涓€涓?鎷夸竴鍗?
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

    // 鏇存柊鏈湴鐘舵€?
    inv.setCursorItem(newCursor);
    if (containerId > 0) {
        inv.setContainerLocalSlot(slotNum, newClicked);
    } else {
        inv.setLocalSlot(slotNum, newClicked);
    }

    // 鏋勫缓 ProtocolCraft Slot 瀵硅薄
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

    // ChangedSlots: 鍛婄煡鏈嶅姟鍣ㄧ偣鍑诲悗妲戒綅鐨勬柊鐘舵€?
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
    if (containerId <= 0) return; // 0=鐜╁鑳屽寘锛屼笉闇€瑕佸叧闂寘

    // 鍏抽棴瀹瑰櫒鍓嶏紝灏嗗鍣ㄦ暟鎹腑鐨勮儗鍖呴儴鍒嗗悓姝ュ洖 slots
    // 宸ヤ綔鍙板鍣ㄥ竷灞€: slots 10-36=涓昏儗鍖? 37-45=蹇嵎鏍?
    // 鐜╁鐗╁搧鏍忓竷灞€: slots 9-35=涓昏儗鍖? 36-44=蹇嵎鏍?
    auto& inv = PlayerInventory::getInstance();
    const auto& containerSlots = inv.getContainerSlots();
    if (containerSlots.size() >= 46) {
        std::vector<InvSlot> updatedSlots(inv.getSlotCount());
        // 宸叉湁 slots 鏁版嵁涓繚鐣?crafting/armor/offhand锛?-8,45锛?
        for (int i = 0; i < inv.getSlotCount() && i < 46; i++) {
            if (i >= 0 && i < 9) {
                // slots 0-8: keep existing (2脳2 craft + armor)
                updatedSlots[i] = inv.getSlot(i);
            } else if (i == 45) {
                updatedSlots[i] = inv.getSlot(i); // offhand
            }
        }
        // 涓昏儗鍖? containerSlots[10..36] 鈫?slots[9..35]
        for (int i = 0; i < 27 && 10 + i < (int)containerSlots.size() && 9 + i < (int)updatedSlots.size(); i++) {
            updatedSlots[9 + i] = containerSlots[10 + i];
        }
        // 蹇嵎鏍? containerSlots[37..45] 鈫?slots[36..44]
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
    // 鍘熺増MC QUICK_CRAFT 鍗忚锛?
    // phase 0 = 寮€濮嬫嫋鎷斤紙buttonNum = type<<2 | 0锛?
    // phase 1 = 鎷栬繃妲戒綅锛坆uttonNum = type<<2 | 1锛?
    // phase 2 = 缁撴潫鎷栨嫿锛坆uttonNum = type<<2 | 2锛?
    // type: 0=鍧囧垎(CHARITABLE), 1=姣忔牸1涓?GREEDY), 2=澶嶅埗(CLONE,鍒涢€犳ā寮?
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    auto& inv = PlayerInventory::getInstance();

    // 璁＄畻 buttonNum: (type << 2) | phase
    // type 0=宸﹂敭鍧囧垎, type 1=鍙抽敭姣忔牸1涓?
    int type = (button == 0) ? 0 : 1;
    int buttonNum = (type << 2) | phase;

    ProtocolCraft::ServerboundContainerClickPacket clickPacket;
    // 浣跨敤褰撳墠鎵撳紑鐨勫鍣↖D锛?=鐜╁鑳屽寘锛?
    int qcContainerId = GameUI::getInstance().getOpenContainerId();
    clickPacket.SetContainerId((qcContainerId >= 0) ? qcContainerId : 0);
    clickPacket.SetStateId(inv.getStateId());

    if (phase == 0 || phase == 2) {
        // 寮€濮?缁撴潫锛歴lotNum 涓?-999锛堣〃绀虹偣鍑诲湪鑳屽寘澶栵級
        clickPacket.SetSlotNum(-999);
    } else {
        // 鎷栬繃妲戒綅锛氫娇鐢ㄥ疄闄呮Ы浣嶅彿
        clickPacket.SetSlotNum((short)slotNum);
    }

    clickPacket.SetButtonNum((char)buttonNum);
    clickPacket.SetClickType(5);  // QUICK_CRAFT = 5

    // ChangedSlots: 鎷栨嫿鎿嶄綔鐢辨湇鍔″櫒澶勭悊锛屽鎴风鍙彂閫佺姸鎬?
    std::map<short, ProtocolCraft::Slot> changed;
    clickPacket.SetChangedSlots(changed);

    // 鍏夋爣鐗╁搧
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
    // 娓呯悊鎵€鏈夊疄浣?
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

        // 绾枃鏈被鍨嬶細{"text": "..."}
        if (j.contains("text") && j["text"].is_string() && !j.contains("translate")) {
            return j["text"].get<std::string>();
        }

        // 缈昏瘧绫诲瀷锛歿"translate": "key", "with": [...]}
        if (!j.contains("translate") || !j["translate"].is_string()) return raw;

        std::string translateKey = j["translate"].get<std::string>();
        auto it = translations.find(translateKey);
        if (it == translations.end()) {
            // 鏃犵炕璇戯紝灏濊瘯 text 瀛楁浣滀负鍥為€€
            return j.contains("text") && j["text"].is_string()
                ? j["text"].get<std::string>() : raw;
        }

        std::string result = it->second;

        // 瑙ｆ瀽 with 鏁扮粍涓殑鍙傛暟
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

        // 鏇挎崲 %1$s, %2$s ...锛堝甫浣嶇疆缂栧彿锛?
        for (size_t i = 0; i < args.size(); i++) {
            std::string placeholder = "%" + std::to_string(i + 1) + "$s";
            size_t pos = 0;
            while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                result.replace(pos, placeholder.length(), args[i]);
                pos += args[i].length();
            }
        }

        // 鏇挎崲 %s锛圝ava 闈炰綅缃牸寮忥紝鎸夐『搴忓尮閰嶏級
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
