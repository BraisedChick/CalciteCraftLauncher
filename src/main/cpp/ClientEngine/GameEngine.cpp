#include "GameEngine.h"
#include "ClientEngine.h"
#include "NetworkManager/NetworkManager.h"
#include "AESEncrypter.h"
#include "Compression.h"
#include "ChunkManager.h"
#include "Renderer/GLRenderer.h"
#include "Renderer/ChunkMeshScheduler.h"
#include "utils.h"
#include "MinecraftVersion.h"
#include "Camera.h"
#include "Collision.h"
#include "Raycast.h"

// ProtocolCraft 头文件
#include "protocolCraft/BinaryReadWrite.hpp"
#include "protocolCraft/Packets/Handshake/Serverbound/ServerboundClientIntentionPacket.hpp"

// ProtocolCraft 头文件 - 登录阶段
#include "protocolCraft/Packets/Login/Serverbound/ServerboundHelloPacket.hpp"
#include "protocolCraft/Packets/Login/Clientbound/ClientboundLoginCompressionPacket.hpp"
#include "protocolCraft/Packets/Login/Clientbound/ClientboundGameProfilePacket.hpp"
#include "protocolCraft/Packets/Login/Clientbound/ClientboundLoginDisconnectPacket.hpp"

// ProtocolCraft 头文件 - 游戏阶段
#include "protocolCraft/Packets/Game/Serverbound/ServerboundKeepAlivePacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundAcceptTeleportationPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundMovePlayerPacketPosRot.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundMovePlayerPacketStatusOnly.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundClientInformationPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundSetCarriedItemPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundContainerClickPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundContainerClosePacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundUseItemOnPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundUseItemPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundPlayerActionPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundInteractPacket.hpp"
#include "protocolCraft/Packets/Game/Serverbound/ServerboundClientCommandPacket.hpp"
// 聊天包
#include "protocolCraft/Packets/Game/Serverbound/ServerboundChatPacket.hpp"
#include "Light.h"
#include "EntityManager.h"
#include "GLEntityRenderer.h"
#include "protocolCraft/Types/NBT/Tag.hpp"
#include "protocolCraft/Utilities/Json.hpp"
#include "3rdparty/json.hpp"
#include "BiomeColorManager.h"
#include "BlockRegistry.h"
#include "PlayerInventory.h"
#include "gui/GameUI.h"
#include "JniBridge.h"

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <cmath>
#include <chrono>
#include <sstream>

using njson = nlohmann::json;

GameEngine::GameEngine(ClientEngine* client)
    : m_client(client),
      chunkManager(nullptr),
      m_inventory(std::make_unique<PlayerInventory>()),
      m_entityManager(std::make_unique<EntityManager>()),
      m_collision(std::make_unique<Collision>()),
      m_light(std::make_unique<Light>()) {
    // 射线检测器：注入 this，运行时惰性获取 chunkManager/entityManager 等
    m_raycast = std::make_unique<Raycast>(this);
}

GameEngine::~GameEngine() {
    // 1. 断开网络
    if (net) net->disconnect();

    // 2. 清除外部模块对内部对象的引用
    if (chunkManager) {
        m_collision->setChunkManager(nullptr);
        m_light->setChunkManager(nullptr);
        if (getMeshScheduler()) getMeshScheduler()->setChunkManager(nullptr);
    }

    // 3. 智能指针自动清理其余资源
}

GLRenderer* GameEngine::getRenderer() {
    return m_client ? m_client->getRenderer() : nullptr;
}

ChunkMeshScheduler* GameEngine::getMeshScheduler() {
    return m_client ? m_client->getMeshScheduler() : nullptr;
}

// 相机眼睛所在方块是否为实心不透明方块（对齐原版 LevelRenderer 旁观穿地兵底：
// 相机嵌入地形时关闭遮挡剔除，只保留视锥剔除。实心口径与面剔除/遮光一致）
bool GameEngine::isEyeInsideOpaqueBlock(double eyeX, double eyeY, double eyeZ) const {
    if (!chunkManager) return false;
    auto* reg = ClientEngine::getInstance() ? ClientEngine::getInstance()->getBlockRegistry() : nullptr;
    if (!reg) return false;
    int bx = (int)std::floor(eyeX);
    int by = (int)std::floor(eyeY);
    int bz = (int)std::floor(eyeZ);
    auto chunk = chunkManager->getChunk(bx >> 4, bz >> 4);
    if (!chunk) return false;
    uint32_t st = chunk->getBlockState(bx & 15, by, bz & 15);
    if (st == 0) return false;
    const auto& meta = reg->getBlockMetadata(st);
    return meta.isFullBlock && meta.isOpaque;
}

void GameEngine::setAuthInfo(const std::string& accessToken, const std::string& uuid, const std::string& tokenType) {
    this->accessToken = accessToken;
    playerUuid = uuid;
    this->tokenType = tokenType;
    premium = !accessToken.empty();
    LOGI("Auth info set: premium=%d, uuid=%s", premium, playerUuid.c_str());
}

bool GameEngine::handleEncryptionRequest(
    const std::string& serverID,
    const std::vector<unsigned char>& publicKey,
    const std::vector<unsigned char>& verifyToken,
    std::vector<unsigned char>& sharedSecret,
    std::vector<unsigned char>& encryptedSecret,
    std::vector<unsigned char>& encryptedVerifyToken) {

    if (!JniBridge::getJvm() || !JniBridge::getActivity()) {
        LOGE("Cannot handle encryption request: JVM or Activity not available");
        return false;
    }

    JNIEnv* env;
    bool attached = false;
    int getEnvResult = JniBridge::getJvm()->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvResult == JNI_EDETACHED) {
        if (JniBridge::getJvm()->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    } else if (getEnvResult != JNI_OK) {
        return false;
    }

    // 创建 Java 参数
    jstring jServerID = env->NewStringUTF(serverID.c_str());

    jbyteArray jPublicKey = env->NewByteArray(static_cast<jsize>(publicKey.size()));
    env->SetByteArrayRegion(jPublicKey, 0, static_cast<jsize>(publicKey.size()),
                            reinterpret_cast<const jbyte*>(publicKey.data()));

    jbyteArray jVerifyToken = env->NewByteArray(static_cast<jsize>(verifyToken.size()));
    env->SetByteArrayRegion(jVerifyToken, 0, static_cast<jsize>(verifyToken.size()),
                            reinterpret_cast<const jbyte*>(verifyToken.data()));

    jstring jAccessToken = env->NewStringUTF(accessToken.c_str());
    jstring jPlayerUuid = env->NewStringUTF(playerUuid.c_str());

    // 调用 MainActivity.handleEncryptionRequest
    jclass clazz = env->GetObjectClass(JniBridge::getActivity());
    jmethodID method = env->GetMethodID(clazz, "handleEncryptionRequest",
        "(Ljava/lang/String;[B[BLjava/lang/String;Ljava/lang/String;)[B");

    jbyteArray jResult = nullptr;
    if (method) {
        jResult = (jbyteArray)env->CallObjectMethod(JniBridge::getActivity(), method,
            jServerID, jPublicKey, jVerifyToken, jAccessToken, jPlayerUuid);

        if (env->ExceptionCheck()) {
            LOGE("Java handleEncryptionRequest threw exception");
            env->ExceptionDescribe();
            env->ExceptionClear();
            jResult = nullptr;
        }
    } else {
        LOGE("handleEncryptionRequest method not found");
    }

    bool success = false;
    if (jResult != nullptr) {
        jsize resultLen = env->GetArrayLength(jResult);
        jbyte* resultBytes = env->GetByteArrayElements(jResult, nullptr);

        const uint8_t* data = reinterpret_cast<const uint8_t*>(resultBytes);
        size_t offset = 0;

        if (resultLen >= 4) {
            int32_t ssLen = (data[offset] << 24) | (data[offset+1] << 16) | (data[offset+2] << 8) | data[offset+3];
            offset += 4;
            if (offset + ssLen <= (size_t)resultLen && ssLen > 0) {
                sharedSecret.assign(data + offset, data + offset + ssLen);
                offset += ssLen;
            }

            if (offset + 4 <= (size_t)resultLen) {
                int32_t esLen = (data[offset] << 24) | (data[offset+1] << 16) | (data[offset+2] << 8) | data[offset+3];
                offset += 4;
                if (offset + esLen <= (size_t)resultLen && esLen > 0) {
                    encryptedSecret.assign(data + offset, data + offset + esLen);
                    offset += esLen;
                }
            }

            if (offset + 4 <= (size_t)resultLen) {
                int32_t evtLen = (data[offset] << 24) | (data[offset+1] << 16) | (data[offset+2] << 8) | data[offset+3];
                offset += 4;
                if (offset + evtLen <= (size_t)resultLen && evtLen > 0) {
                    encryptedVerifyToken.assign(data + offset, data + offset + evtLen);
                    offset += evtLen;
                }
            }

            success = !sharedSecret.empty() && !encryptedSecret.empty() && !encryptedVerifyToken.empty();
        }

        env->ReleaseByteArrayElements(jResult, resultBytes, JNI_ABORT);
        LOGI("Encryption request handled: sharedSecret_len=%zu, encSecret_len=%zu, encVerifyToken_len=%zu",
             sharedSecret.size(), encryptedSecret.size(), encryptedVerifyToken.size());
    } else {
        LOGE("handleEncryptionRequest returned null");
    }

    // 清理 JNI 引用
    env->DeleteLocalRef(jServerID);
    env->DeleteLocalRef(jPublicKey);
    env->DeleteLocalRef(jVerifyToken);
    env->DeleteLocalRef(jAccessToken);
    env->DeleteLocalRef(jPlayerUuid);
    if (jResult) env->DeleteLocalRef(jResult);
    env->DeleteLocalRef(clazz);

    if (attached) {
        JniBridge::detachCurrentThread();
    }

    return success;
}

bool GameEngine::start(const std::string& host, int port) {
    LOGI("========== Starting client ==========");
    LOGI("Server: %s:%d", host.c_str(), port);
    LOGI("Username: %s", ClientEngine::getUsername().c_str());

    disconnectReason.clear();
    loginCompleted = false;

    // 初始化压缩状态
    Compression::setEnabled(false);
    Compression::setThreshold(-1);
    Compression::setReceiveEnabled(false);

    chunkManager = std::make_unique<ChunkManager>();

    // chunkManager 刚创建，通知相关模块更新指针
    if (getMeshScheduler()) {
        getMeshScheduler()->setChunkManager(chunkManager.get());
    }
    m_collision->setChunkManager(chunkManager.get());
    m_light->setChunkManager(chunkManager.get());
    LOGI("ChunkManager created and linked to scheduler/collision/light");

    net = std::make_unique<NetworkManager>();
    if (!net->connect(host, port)) {
        LOGE("Failed to connect to %s:%d", host.c_str(), port);
        // 原版风格：展示底层错误原因（如 "Connection refused: getsockopt"）
        disconnectReason = net->getLastError().empty() ? "无法连接到服务器" : net->getLastError();
        return false;
    }
    LOGI("Network connection established");

    // ========== 握手阶段 ==========
    {
        LOGI("Sending handshake packet via ProtocolCraft");

        int protocolVersion = PROTOCOL_VERSION;

        ProtocolCraft::ServerboundClientIntentionPacket handshake;
        handshake.SetProtocolVersion(protocolVersion);
        handshake.SetHostName(host);
        handshake.SetPort(port);
        handshake.SetIntention(2);  // 2 = LOGIN state

        LOGI("Using protocol version: %d (%s)",
             protocolVersion,
             getProtocolVersionName(protocolVersion));

        ProtocolCraft::WriteContainer writeData;
        handshake.Write(writeData);

        LOGI("Handshake packet size: %zu bytes", writeData.size());
        if (!sendPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()))) {
            LOGE("Failed to send handshake");
            disconnectReason = "发送握手数据包失败";
            net->disconnect();
            return false;
        }
    }

    // ========== 登录阶段 - 发送 Login Start ==========
    {
        LOGI("Sending login start: %s", ClientEngine::getUsername().c_str());

        ProtocolCraft::ServerboundHelloPacket loginStart;
        #if PROTOCOL_VERSION < 759
                loginStart.SetGameProfile(ClientEngine::getUsername());
        #else
                loginStart.SetName_(ClientEngine::getUsername());
        #endif

        ProtocolCraft::WriteContainer writeData;
        loginStart.Write(writeData);

        LOGI("LoginStart packet size: %zu bytes", writeData.size());
        if (!sendPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()))) {
            LOGE("Failed to send login start");
            disconnectReason = "发送登录数据包失败";
            net->disconnect();
            return false;
        }
    }

    // ========== 登录阶段 - 接收响应 ==========
    while (true) {
        auto resp = net->receivePacket();
        if (resp.empty()) {
            LOGE("Empty response during login");
            disconnectReason = "服务器关闭了连接";
            net->disconnect();
            return false;
        }

        size_t pos = 0;
        ProtocolCraft::ReadIterator iter = resp.cbegin();
        size_t remaining = resp.size();
        int pid = ProtocolCraft::ReadData<int, ProtocolCraft::VarInt>(iter, remaining);
        pos = resp.size() - remaining;

        switch (pid) {
        case 0x02: {
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

            goto loginDone;
        }

        case 0x01: {
            LOGI("Received Encryption Request (online mode server)");

            if (!premium) {
                LOGE("Server is in online mode, but no premium auth available");
                disconnectReason = "该服务器开启了正版验证，请先登录正版账号";
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

                bool encResult = handleEncryptionRequest(
                    serverID, publicKey, verifyToken,
                    rawSharedSecret, encryptedSharedSecret, encryptedVerifyToken);

                if (!encResult || rawSharedSecret.empty()) {
                    LOGE("Failed to handle encryption request via Java");
                    disconnectReason = "正版验证失败：无法完成加密握手";
                    net->disconnect();
                    return false;
                }
                LOGI("Java encryption handling successful, sharedSecret_len=%zu, encSecret_len=%zu, encVerifyToken_len=%zu",
                     rawSharedSecret.size(), encryptedSharedSecret.size(), encryptedVerifyToken.size());

                aesEncrypter = std::make_unique<AESEncrypter>();
                aesEncrypter->Init(rawSharedSecret);

                if (!aesEncrypter->isInitialized()) {
                    LOGE("Failed to initialize AESEncrypter");
                    disconnectReason = "加密初始化失败";
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
                    disconnectReason = "发送加密密钥失败";
                    net->disconnect();
                    return false;
                }
                LOGI("Encryption key response sent");

                net->setEncrypter(aesEncrypter.get());
                LOGI("AES-128-CFB8 stream encryption enabled on NetworkManager");

            } catch (const std::exception& e) {
                LOGE("Failed to parse encryption request: %s", e.what());
                disconnectReason = "解析加密请求失败";
                net->disconnect();
                return false;
            }
            continue;
        }

        case 0x03: {
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
            ProtocolCraft::ClientboundLoginDisconnectPacket disconnectPacket;
            std::vector<unsigned char> packetData(resp.begin() + pos, resp.end());
            auto iter = packetData.cbegin();
            size_t length = packetData.size();

            // 服务器拒绝登录（版本不匹配、白名单等）：解析 Chat 组件展示具体原因
            try {
                disconnectPacket.Read(iter, length);
                std::string rawJson = disconnectPacket.GetReason().GetRawText();
                std::string reason = rawJson.empty() ? disconnectPacket.GetReason().GetText()
                                                     : parseChatComponent(rawJson);
                disconnectReason = reason.empty() ? "登录被服务器拒绝" : reason;
                LOGE("Disconnected during login: %s", disconnectReason.c_str());
            } catch (...) {
                disconnectReason = "登录被服务器拒绝";
                LOGE("Disconnected during login (failed to parse reason)");
            }
            net->disconnect();
            return false;
        }

        default: {
            LOGE("Unexpected login packet: %d", pid);
            disconnectReason = "意外的登录数据包 (id=" + std::to_string(pid) + ")";
            net->disconnect();
            return false;
        }
        }
    }

loginDone:
    loginCompleted = true;
    if (Compression::isReceiveEnabled()) {
        Compression::setEnabled(true);
        LOGI("Compression fully enabled (threshold=%d)", Compression::getThreshold());
    } else {
        LOGI("Compression not enabled by server (offline mode)");
    }

    // 登录成功，此时才切换到游戏状态（连接/登录失败时保持 CONNECTING，
    // 由 ClientEngine 的连接线程收尾回到标题界面，避免进入空世界）
    GameUI::getInstance().setState(UIState::IN_GAME);
    GameUI::getInstance().clearChatMessages();

    // 委托 NetworkManager 处理 PLAY 状态
    net->setEngine(this);
    net->registerHandlers();
    net->startPlayLoop();  // 阻塞直到连接关闭

    net->disconnect();
    return true;
}

bool GameEngine::sendPacket(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net) return false;
    return net->sendRawPacket(data);
}

bool GameEngine::isConnected() const {
    std::lock_guard<std::mutex> lock(netMutex);
    return net && net->isConnected();
}

void GameEngine::sendPlayerMovement(double x, double y, double z, float yaw, float pitch, bool onGround) {
    if (!movementEnabled.load()) return;
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

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

void GameEngine::sendHeldItemChange(int slot) {
    if (slot < 0 || slot > 8) return;
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::ServerboundSetCarriedItemPacket heldPacket;
    heldPacket.SetSlot(slot);

    ProtocolCraft::WriteContainer writeData;
    heldPacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
}

void GameEngine::sendBlockPlacement(int blockX, int blockY, int blockZ, int face, int hand) {
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

void GameEngine::sendUseItem(int hand) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::ServerboundUseItemPacket usePacket;
    usePacket.SetHand(hand);

    ProtocolCraft::WriteContainer writeData;
    usePacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
    LOGI("Sent UseItem: hand=%d", hand);
}

void GameEngine::sendBlockBreakStart(int blockX, int blockY, int blockZ, int face) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::NetworkPosition pos;
    pos.SetX(blockX);
    pos.SetY(blockY);
    pos.SetZ(blockZ);

    ProtocolCraft::ServerboundPlayerActionPacket startDig;
    startDig.SetAction(0);
    startDig.SetPos(pos);
    startDig.SetDirection(static_cast<char>(face));

    ProtocolCraft::WriteContainer writeStart;
    startDig.Write(writeStart);
    net->sendRawPacket(std::vector<uint8_t>(writeStart.begin(), writeStart.end()));
}

void GameEngine::sendBlockBreakFinish(int blockX, int blockY, int blockZ, int face) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::NetworkPosition pos;
    pos.SetX(blockX);
    pos.SetY(blockY);
    pos.SetZ(blockZ);

    ProtocolCraft::ServerboundPlayerActionPacket finishDig;
    finishDig.SetAction(2);
    finishDig.SetPos(pos);
    finishDig.SetDirection(static_cast<char>(face));

    ProtocolCraft::WriteContainer writeFinish;
    finishDig.Write(writeFinish);
    net->sendRawPacket(std::vector<uint8_t>(writeFinish.begin(), writeFinish.end()));
}

void GameEngine::sendBlockBreakAbort(int blockX, int blockY, int blockZ, int face) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::NetworkPosition pos;
    pos.SetX(blockX);
    pos.SetY(blockY);
    pos.SetZ(blockZ);

    ProtocolCraft::ServerboundPlayerActionPacket abortDig;
    abortDig.SetAction(1);
    abortDig.SetPos(pos);
    abortDig.SetDirection(static_cast<char>(face));

    ProtocolCraft::WriteContainer writeAbort;
    abortDig.Write(writeAbort);
    net->sendRawPacket(std::vector<uint8_t>(writeAbort.begin(), writeAbort.end()));
}

void GameEngine::sendEntityAttack(int entityId) {
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

void GameEngine::sendRespawn() {
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

void GameEngine::sendChatMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::ServerboundChatPacket chatPacket;
    chatPacket.SetMessage(message);
#if PROTOCOL_VERSION >= 759
    chatPacket.SetTimestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
#if PROTOCOL_VERSION < 760
    chatPacket.SetSignedPreview(false);
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

void GameEngine::sendContainerClick(int slotNum, int button, int containerId) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    if (containerId < 0) {
        int openId = GameUI::getInstance().getOpenContainerId();
        containerId = (openId >= 0) ? openId : 0;
    }

    auto& inv = *m_inventory;
    InvSlot cursor = inv.getCursorItem();
    InvSlot clicked = (containerId > 0) ? inv.getContainerSlot(slotNum) : inv.getSlot(slotNum);

    InvSlot newCursor = cursor;
    InvSlot newClicked = clicked;

    if (button == 0) {  // 左键
        if (!cursor.present && clicked.present) {
            newCursor = clicked;
            newClicked = InvSlot{};
        } else if (cursor.present && !clicked.present) {
            newClicked = cursor;
            newCursor = InvSlot{};
        } else if (cursor.present && clicked.present && cursor.itemId == clicked.itemId) {
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
            newCursor = clicked;
            newClicked = cursor;
        }
    } else if (button == 1) {  // 右键
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

    inv.setCursorItem(newCursor);
    if (containerId > 0) {
        inv.setContainerLocalSlot(slotNum, newClicked);
    } else {
        inv.setLocalSlot(slotNum, newClicked);
    }

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

    std::map<short, ProtocolCraft::Slot> changed;
    changed[(short)slotNum] = toSlot(newClicked);
    clickPacket.SetChangedSlots(changed);

    clickPacket.SetCarriedItem(toSlot(newCursor));

    ProtocolCraft::WriteContainer writeData;
    clickPacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
}

void GameEngine::sendContainerClose() {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    int containerId = GameUI::getInstance().getOpenContainerId();
    if (containerId <= 0) return;

    auto& inv = *m_inventory;
    const auto& containerSlots = inv.getContainerSlots();
    if (containerSlots.empty()) return;

    // ---- 根据容器类型计算背包偏移 ----
    int containerType = GameUI::getInstance().getOpenContainerType();
    int mainStart = 0, hotbarStart = 0;

    if (containerType == 2 || containerType == 5) { // 箱子
        int rows = (containerType == 5) ? 6 : 3;
        mainStart = rows * 9;
        hotbarStart = mainStart + 27;
    } else if (containerType == 11) { // 工作台
        mainStart = 10;
        hotbarStart = 37;
    } else if (containerType == 13) { // 熔炉
        mainStart = 3;
        hotbarStart = 30;
    } else {
        // 未知类型：从末尾倒数36格（通用降级）
        if (containerSlots.size() >= 36) {
            mainStart = (int)containerSlots.size() - 36;
            hotbarStart = mainStart + 27;
        } else {
            return; // 无法确定，跳过同步
        }
    }

    // ---- 构建完整更新后的物品栏 ----
    std::vector<InvSlot> updatedSlots(inv.getSlotCount());
    // 复制所有当前槽位（保护合成格、装备、副手等）
    for (int i = 0; i < inv.getSlotCount(); ++i) {
        updatedSlots[i] = inv.getSlot(i);
    }

    // 更新主背包（slots 9~35）
    for (int i = 0; i < 27 && mainStart + i < (int)containerSlots.size(); ++i) {
        updatedSlots[9 + i] = containerSlots[mainStart + i];
    }
    // 更新快捷栏（slots 36~44）
    for (int i = 0; i < 9 && hotbarStart + i < (int)containerSlots.size(); ++i) {
        updatedSlots[36 + i] = containerSlots[hotbarStart + i];
    }

    inv.setContent(0, updatedSlots);
    LOGI("ContainerClose: synced from container type %d", containerType);

    // ---- 发送关闭包 ----
    ProtocolCraft::ServerboundContainerClosePacket closePacket;
    closePacket.SetContainerId((unsigned char)containerId);
    ProtocolCraft::WriteContainer writeData;
    closePacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
    LOGI("Sent ContainerClose: id=%d", containerId);
}

void GameEngine::sendContainerQuickCraft(int phase, int slotNum, int button) {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    auto& inv = *m_inventory;

    int type = (button == 0) ? 0 : 1;
    int buttonNum = (type << 2) | phase;

    ProtocolCraft::ServerboundContainerClickPacket clickPacket;
    int qcContainerId = GameUI::getInstance().getOpenContainerId();
    clickPacket.SetContainerId((qcContainerId >= 0) ? qcContainerId : 0);
    clickPacket.SetStateId(inv.getStateId());

    if (phase == 0 || phase == 2) {
        clickPacket.SetSlotNum(-999);
    } else {
        clickPacket.SetSlotNum((short)slotNum);
    }

    clickPacket.SetButtonNum((char)buttonNum);
    clickPacket.SetClickType(5);  // QUICK_CRAFT

    std::map<short, ProtocolCraft::Slot> changed;
    clickPacket.SetChangedSlots(changed);

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

void GameEngine::disconnect() {
    std::lock_guard<std::mutex> lock(netMutex);
    if (net) {
        net->disconnect();
    }
    m_entityManager->removeAllEntities();
    if (m_client && m_client->getEntityRenderer()) {
        m_client->getEntityRenderer()->clearTextureCache();
    }
}

float GameEngine::getSkyDarken() const {
    return m_light->getSkyDarken();
}

long long GameEngine::getWorldDayTime() const {
    return m_light->getWorldDayTime();
}

std::string GameEngine::parseChatComponent(const std::string& raw) const {
    try {
        auto j = njson::parse(raw, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return raw;

        std::string result;

        // 1. 处理 text / translate 主内容
        if (j.contains("translate") && j["translate"].is_string()) {
            std::string translateKey = j["translate"].get<std::string>();
            const std::string* tr = m_client ? m_client->translate(translateKey) : nullptr;
            if (tr) {
                result = *tr;

                // 递归解析 with 元素：每个元素本身就是一个完整的 Chat Component
                std::vector<std::string> args;
                if (j.contains("with") && j["with"].is_array()) {
                    for (const auto& elem : j["with"]) {
                        if (elem.is_string()) {
                            args.push_back(elem.get<std::string>());
                        } else if (elem.is_object()) {
                            args.push_back(parseChatComponent(elem.dump()));
                        } else {
                            args.push_back("");
                        }
                    }
                }

                // 替换 %N$s 位置占位符
                for (size_t i = 0; i < args.size(); i++) {
                    std::string placeholder = "%" + std::to_string(i + 1) + "$s";
                    size_t pos = 0;
                    while ((pos = result.find(placeholder, pos)) != std::string::npos) {
                        result.replace(pos, placeholder.length(), args[i]);
                        pos += args[i].length();
                    }
                }

                // 替换顺序 %s 占位符
                size_t argIdx = 0;
                size_t pos = 0;
                while ((pos = result.find("%s", pos)) != std::string::npos && argIdx < args.size()) {
                    result.replace(pos, 2, args[argIdx]);
                    pos += args[argIdx].length();
                    argIdx++;
                }
            } else if (j.contains("text") && j["text"].is_string()) {
                // translate 找不到，回退到 text 字段
                result = j["text"].get<std::string>();
            } else {
                // translate 找不到且无 text，回退原 JSON
                return raw;
            }
        } else if (j.contains("text") && j["text"].is_string()) {
            // 纯文本组件（无 translate）
            result = j["text"].get<std::string>();
        } else {
            // 既无 text 也无 translate，回退原 JSON
            return raw;
        }

        // 2. 拼接 extra 数组（原版 Chat Component 标准子组件字段）
        // 每个 extra 元素本身就是一个完整的 Chat Component，递归解析
        if (j.contains("extra") && j["extra"].is_array()) {
            for (const auto& elem : j["extra"]) {
                if (elem.is_string()) {
                    result += elem.get<std::string>();
                } else if (elem.is_object()) {
                    result += parseChatComponent(elem.dump());
                }
            }
        }

        return result;
    } catch (...) {
        return raw;
    }
}
