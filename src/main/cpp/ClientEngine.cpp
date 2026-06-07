#include "ClientEngine.h"
#include "NetworkManager.h"
#include "Compression.h"
#include "ChunkManager.h"
#include "GLRenderer.h"
#include "utils.h"
#include "MinecraftVersion.h"
#include "CameraController.h"
#include "Collision.h"

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
#include "protocolCraft/include/protocolCraft/Packets/Game/Serverbound/ServerboundMovePlayerPacketStatusOnly.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Serverbound/ServerboundClientInformationPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Serverbound/ServerboundSetCarriedItemPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Serverbound/ServerboundClientCommandPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundLoginPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundBlockUpdatePacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundSectionBlocksUpdatePacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundContainerSetContentPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundContainerSetSlotPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundSetHealthPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundPlayerCombatKillPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundGameEventPacket.hpp"
#include "protocolCraft/include/protocolCraft/Packets/Game/Clientbound/ClientboundRespawnPacket.hpp"
#include "protocolCraft/include/protocolCraft/Types/NBT/Tag.hpp"
#include "protocolCraft/include/protocolCraft/Utilities/Json.hpp"
#include "BiomeColorManager.h"
#include "PlayerInventory.h"

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <map>
#include <cmath>
#include <chrono>

ClientEngine* ClientEngine::instance = nullptr;

ClientEngine::ClientEngine() : chunkManager(nullptr) {
    instance = this;
}

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

    net = std::make_unique<NetworkManager>();
    if (!net->connect(host, port)) {
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
        loginStart.SetGameProfile(username);  // 协议 758 使用 GameProfile 字段

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
            net->disconnect();
            return false;
        } else {
            LOGE("Unexpected login packet: %d", pid);
            net->disconnect();
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
        sendPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
        LOGI("Sent Client Information (ViewDistance=10)");
    }

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

        // 紧急包（延迟敏感）：位置、方块更新、生命、保活、游戏事件
        if (pid == 0x38 || pid == 0x0C || pid == 0x3F || pid == 0x52 ||
            pid == 0x21 || pid == 0x1E || pid == 0x3D) {
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

void ClientEngine::sendRespawn() {
    std::lock_guard<std::mutex> lock(netMutex);
    if (!net || !net->isConnected()) return;

    ProtocolCraft::ServerboundClientCommandPacket cmdPacket;
    cmdPacket.SetAction(0);  // PERFORM_RESPAWN

    ProtocolCraft::WriteContainer writeData;
    cmdPacket.Write(writeData);
    net->sendRawPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
    LOGI("Sent respawn request (ClientCommand PERFORM_RESPAWN)");
}

void ClientEngine::disconnect() {
    std::lock_guard<std::mutex> lock(netMutex);
    if (net) {
        net->disconnect();
    }
}

void ClientEngine::loadLanguage(const std::string& json) {
    try {
        auto root = ProtocolCraft::Json::Parse(json);
        if (!root.is_object()) {
            LOGE("Language file is not a JSON object");
            return;
        }
        const auto& obj = root.get_object();
        for (const auto& [key, value] : obj) {
            if (value.is_string()) {
                translations[key] = value.get_string();
            }
        }
        LOGI("Loaded %zu translations", translations.size());
    } catch (const std::exception& e) {
        LOGE("Failed to parse language file: %s", e.what());
    } catch (...) {
        LOGE("Failed to parse language file: unknown error");
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
            case 0x26: { // Login (Play) - 玩家初始状态
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

                    // Minecraft 1.18+ standard Overworld dimension
                    // min_y = -64, height = 384 (Y range: -64 to 320)
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

                    bool sent = sendPacket(response);
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

            case 0x22: { // Chunk Data (Level Chunk with Light) — 入队异步处理，不阻塞网络循环
                {
                    std::lock_guard<std::mutex> lock(chunkQueueMutex);
                    chunkQueue.push({std::vector<uint8_t>(data.begin() + startPos, data.end())});
                }
                chunkCV.notify_one();
                break;
            }

            case 0x0C: { // Block Update（单一方块更新）
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

                LOGI("BlockUpdate: chunk(%d,%d) pos=(%d,%d,%d) state=%d",
                     chunkX, chunkZ, blockX, blockY, blockZ, blockState);

                if (chunkManager) {
                    auto chunk = chunkManager->getChunk(chunkX, chunkZ);
                    if (chunk) {
                        chunk->setBlockState(localX, blockY, localZ, blockState);
                        if (glRenderer) {
                            glRenderer->markChunkForUpdate(chunkX, chunkZ);
                        }
                    } else {
                        LOGW("BlockUpdate: chunk (%d, %d) not loaded", chunkX, chunkZ);
                    }
                }
                break;
            }

            case 0x3F: { // Section Blocks Update（多方块批量更新）
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

                LOGI("SectionBlocksUpdate: chunk(%d,%d) sectionY=%d entries=%zu",
                     chunkX, chunkZ, sectionY, sectionPacket.GetPosState().size());

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

            case 0x14: { // Container Set Content（设置容器全部物品）
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
                LOGI("Container Set Content: id=%d, slots=%zu, nonEmpty=%d",
                     containerId, items.size(), nonEmptyCount);
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

            case 0x16: { // Container Set Slot（设置单个格子）
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
                LOGI("Container Set Slot: id=%d, slot=%d, present=%d, itemId=%d, count=%d",
                     containerId, slotIndex, is.present, is.itemId, is.count);
                break;
            }
            case 0x35: { // Combat Kill (death message)
                ProtocolCraft::ClientboundPlayerCombatKillPacket killPacket;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                killPacket.Read(iter, len);
                deathMessage = killPacket.GetMessage().GetText();
                if (deathMessage.empty()) {
                    // Chat::ParseChat doesn't handle translate-type death messages.
                    // Use the language file (zh_cn.json) to translate.
                    const std::string& raw = killPacket.GetMessage().GetRawText();
                    try {
                        auto json = ProtocolCraft::Json::Parse(raw);
                        if (json.is_object() && json.contains("translate")) {
                            const std::string& translate = json["translate"].get_string();
                            auto it = translations.find(translate);
                            if (it != translations.end()) {
                                // Found a translation template, e.g. "%1$s被%2$s杀死了"
                                deathMessage = it->second;
                                // Extract text from "with" array
                                std::vector<std::string> args;
                                if (json.contains("with") && json["with"].is_array()) {
                                    for (size_t i = 0; i < json["with"].size(); i++) {
                                        const auto& elem = json["with"][i];
                                        if (elem.contains("text") && elem["text"].is_string()) {
                                            args.push_back(elem["text"].get_string());
                                        } else if (elem.contains("translate") && elem["translate"].is_string()) {
                                            // 实体名称也是 translate 类型，如 entity.minecraft.zombie
                                            const std::string& subKey = elem["translate"].get_string();
                                            auto subIt = translations.find(subKey);
                                            if (subIt != translations.end()) {
                                                args.push_back(subIt->second);
                                            } else {
                                                args.push_back(subKey);
                                            }
                                        } else {
                                            args.push_back("");
                                        }
                                    }
                                }
                                // Replace %1$s, %2$s, %3$s with args
                                for (size_t i = 0; i < args.size(); i++) {
                                    std::string placeholder = "%" + std::to_string(i + 1) + "$s";
                                    size_t pos = 0;
                                    while ((pos = deathMessage.find(placeholder, pos)) != std::string::npos) {
                                        deathMessage.replace(pos, placeholder.length(), args[i]);
                                        pos += args[i].length();
                                    }
                                }
                            } else if (json.contains("text")) {
                                deathMessage = json["text"].get_string();
                            } else {
                                // No translation found, use raw JSON as fallback
                                deathMessage = raw;
                            }
                        } else if (json.contains("text")) {
                            deathMessage = json["text"].get_string();
                        }
                    } catch (...) {
                        deathMessage = raw;
                    }
                }
                LOGI("Death message: '%s'", deathMessage.c_str());
                break;
            }


            case 0x52: { // Set Health（玩家生命/饥饿值更新）
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

            case 0x1E: { // Game Event（包含游戏模式变更）
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

            case 0x3D: { // Respawn（重生，包含新游戏模式）
                ProtocolCraft::ClientboundRespawnPacket respawnPacket;
                std::vector<unsigned char> pktData(data.begin() + startPos, data.end());
                auto iter = pktData.cbegin();
                size_t len = pktData.size();
                respawnPacket.Read(iter, len);
                int newMode = respawnPacket.GetPlayerGameType();
                gameMode = newMode;
                Collision::getInstance().setGameMode(newMode);
                LOGI("Respawn: game mode=%d", newMode);
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