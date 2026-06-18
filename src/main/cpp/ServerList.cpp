#include "ServerList.h"
#include <android/log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <sstream>

// ProtocolCraft 数据包
#include "protocolCraft/Packets/Handshake/Serverbound/ServerboundClientIntentionPacket.hpp"
#include "protocolCraft/Packets/Status/Serverbound/ServerboundStatusRequestPacket.hpp"
#include "protocolCraft/Packets/Status/Serverbound/ServerboundPingRequestPacket.hpp"
#include "protocolCraft/Packets/Status/Clientbound/ClientboundStatusResponsePacket.hpp"
#include "protocolCraft/Packets/Status/Clientbound/ClientboundPongResponsePacket.hpp"

#define SP_LOG_TAG "ServerList"
#define SP_LOGI(...) __android_log_print(ANDROID_LOG_INFO, SP_LOG_TAG, __VA_ARGS__)
#define SP_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, SP_LOG_TAG, __VA_ARGS__)

// ===== VarInt 编解码 =====

static void writeVarInt(std::vector<uint8_t>& buf, int value) {
    while (true) {
        if ((value & ~0x7F) == 0) {
            buf.push_back((uint8_t)value);
            return;
        }
        buf.push_back((uint8_t)((value & 0x7F) | 0x80));
        value >>= 7;
    }
}

static int readVarInt(const uint8_t* data, size_t len, size_t& bytesRead) {
    int result = 0;
    int shift = 0;
    bytesRead = 0;
    for (size_t i = 0; i < len && i < 5; i++) {
        result |= (data[i] & 0x7F) << shift;
        bytesRead++;
        if ((data[i] & 0x80) == 0) return result;
        shift += 7;
    }
    return -1;
}

// ===== 构造完整数据包（长度前缀 + 包体）=====

static std::vector<uint8_t> buildPacket(const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> packet;
    writeVarInt(packet, (int)payload.size());
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

// ===== Socket 辅助 =====

static bool sendAll(int sock, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int n = send(sock, data + sent, len - sent, 0);
        if (n <= 0) return false;
        sent += n;
    }
    return true;
}

static int recvExact(int sock, uint8_t* buf, size_t len, int timeoutMs = 5000) {
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    size_t total = 0;
    while (total < len) {
        int n = recv(sock, buf + total, len - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return (int)total;
}

// ===== 读取一个 Minecraft 数据包 =====

static std::vector<uint8_t> recvMCPacket(int sock, int timeoutMs = 5000) {
    uint8_t lenBuf[5];
    int lenBytes = 0;
    int packetLen = -1;

    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (lenBytes < 5) {
        int n = recv(sock, lenBuf + lenBytes, 1, 0);
        if (n <= 0) return {};
        lenBytes++;
        size_t br = 0;
        packetLen = readVarInt(lenBuf, lenBytes, br);
        if ((lenBuf[lenBytes - 1] & 0x80) == 0) break;
    }
    if (packetLen <= 0 || packetLen > 1048576) return {};

    std::vector<uint8_t> body(packetLen);
    if (recvExact(sock, body.data(), packetLen, timeoutMs) < 0) return {};
    return body;
}

// ===== base64 解码 =====

std::vector<uint8_t> ServerList::base64Decode(const std::string& encoded) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<uint8_t> result;
    int val = 0, bits = -8;
    for (char c : encoded) {
        if (c == '=' || c == '\n' || c == '\r') continue;
        int idx = (int)chars.find(c);
        if (idx < 0) continue;
        val = (val << 6) | idx;
        bits += 6;
        if (bits >= 0) {
            result.push_back((uint8_t)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return result;
}

// ===== 简易 JSON 解析 =====

// 提取 JSON 字符串字段值（从 "key": "value" 格式）
static std::string extractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + searchKey.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++; // skip opening quote
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            if (json[pos] == 'n') result += '\n';
            else if (json[pos] == '"') result += '"';
            else result += json[pos];
        } else {
            result += json[pos];
        }
        pos++;
    }
    return result;
}

// 提取 JSON 整数字段值
static int extractJsonInt(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return -1;
    pos = json.find(':', pos + searchKey.size());
    if (pos == std::string::npos) return -1;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return -1;
    try {
        return std::stoi(json.substr(pos));
    } catch (...) {
        return -1;
    }
}

// 提取 MOTD（description 可能是字符串或对象）
static std::string extractMotd(const std::string& json) {
    std::string searchKey = "\"description\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + searchKey.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
    if (pos >= json.size()) return "";

    // 如果是字符串
    if (json[pos] == '"') {
        return extractJsonString(json, "description");
    }

    // 如果是对象，查找 "text" 字段
    size_t objEnd = json.find('}', pos);
    if (objEnd == std::string::npos) return "";
    std::string obj = json.substr(pos, objEnd - pos + 1);
    return extractJsonString(obj, "text");
}

// ===== 主 Ping 函数 =====

PingResult ServerList::ping(const std::string& host, int port, int protocolVersion) {
    PingResult result;

    // 1. 建立 TCP 连接
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        SP_LOGE("Failed to create socket");
        return result;
    }

    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct hostent* server = gethostbyname(host.c_str());
    if (!server) {
        SP_LOGE("DNS resolution failed: %s", host.c_str());
        close(sock);
        return result;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);
    addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        SP_LOGE("Connect failed: %s:%d", host.c_str(), port);
        close(sock);
        return result;
    }

    // 2. 发送 Handshake（Next State = 1 = Status）
    {
        ProtocolCraft::ServerboundClientIntentionPacket handshake;
        handshake.SetProtocolVersion(protocolVersion);
        handshake.SetHostName(host);
        handshake.SetPort(port);
        handshake.SetIntention(1);

        ProtocolCraft::WriteContainer writeData;
        handshake.Write(writeData);

        auto packet = buildPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
        if (!sendAll(sock, packet.data(), packet.size())) {
            SP_LOGE("Failed to send handshake");
            close(sock);
            return result;
        }
    }

    // 3. 发送 Status Request
    {
        ProtocolCraft::ServerboundStatusRequestPacket statusReq;
        ProtocolCraft::WriteContainer writeData;
        statusReq.Write(writeData);

        auto packet = buildPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
        if (!sendAll(sock, packet.data(), packet.size())) {
            SP_LOGE("Failed to send status request");
            close(sock);
            return result;
        }
    }

    // 4. 接收 Status Response
    {
        auto body = recvMCPacket(sock, 5000);
        if (body.empty()) {
            SP_LOGE("Empty status response");
            close(sock);
            return result;
        }

        size_t br = 0;
        int pid = readVarInt(body.data(), body.size(), br);
        if (pid != 0x00) {
            SP_LOGE("Unexpected packet ID: %d (expected 0x00)", pid);
            close(sock);
            return result;
        }

        ProtocolCraft::ReadIterator iter = body.cbegin() + br;
        size_t remaining = body.size() - br;
        ProtocolCraft::ClientboundStatusResponsePacket resp;
        try {
            resp.Read(iter, remaining);
        } catch (...) {
            SP_LOGE("Failed to parse status response");
            close(sock);
            return result;
        }

        std::string jsonStr = resp.GetStatus();
        SP_LOGI("Status JSON (%zu bytes): %.200s", jsonStr.size(), jsonStr.c_str());

        // 解析 JSON
        result.motd = extractMotd(jsonStr);
        result.versionName = extractJsonString(jsonStr, "name");
        result.protocolVersion = extractJsonInt(jsonStr, "protocol");
        result.onlinePlayers = extractJsonInt(jsonStr, "online");
        result.maxPlayers = extractJsonInt(jsonStr, "max");

        // 服务器图标
        std::string favicon = extractJsonString(jsonStr, "favicon");
        if (!favicon.empty()) {
            const std::string prefix = "data:image/png;base64,";
            if (favicon.size() > prefix.size() && favicon.substr(0, prefix.size()) == prefix) {
                result.faviconPng = base64Decode(favicon.substr(prefix.size()));
                SP_LOGI("Favicon decoded: %zu bytes PNG", result.faviconPng.size());
            }
        }

        result.success = true;
    }

    // 5. 发送 Ping Request
    {
        auto now = std::chrono::steady_clock::now();
        long long timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();

        ProtocolCraft::ServerboundPingRequestStatusPacket pingReq;
        pingReq.SetTime(timestamp);

        ProtocolCraft::WriteContainer writeData;
        pingReq.Write(writeData);

        auto packet = buildPacket(std::vector<uint8_t>(writeData.begin(), writeData.end()));
        auto sendTime = std::chrono::steady_clock::now();

        if (!sendAll(sock, packet.data(), packet.size())) {
            SP_LOGE("Failed to send ping");
            close(sock);
            return result;
        }

        // 6. 接收 Pong Response
        auto pongBody = recvMCPacket(sock, 5000);
        auto recvTime = std::chrono::steady_clock::now();

        if (!pongBody.empty()) {
            size_t br2 = 0;
            int pongPid = readVarInt(pongBody.data(), pongBody.size(), br2);
            if (pongPid == 0x01) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    recvTime - sendTime).count();
                result.latencyMs = (int)elapsed;
                SP_LOGI("Latency: %dms", result.latencyMs);
            }
        }
    }

    close(sock);
    return result;
}
