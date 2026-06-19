#include "ServerList.h"
#include <android/log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <sstream>
#include "3rdparty/json.hpp"
using njson = nlohmann::json;

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

// ===== JSON 解析 (nlohmann/json) =====

// Minecraft 颜色名称 → § 代码
static char colorNameToCode(const std::string& name) {
    if (name == "black") return '0';
    if (name == "dark_blue") return '1';
    if (name == "dark_green") return '2';
    if (name == "dark_aqua") return '3';
    if (name == "dark_red") return '4';
    if (name == "dark_purple") return '5';
    if (name == "gold") return '6';
    if (name == "gray") return '7';
    if (name == "dark_gray") return '8';
    if (name == "blue") return '9';
    if (name == "green") return 'a';
    if (name == "aqua") return 'b';
    if (name == "red") return 'c';
    if (name == "light_purple") return 'd';
    if (name == "yellow") return 'e';
    if (name == "white") return 'f';
    return 0;
}

// 将 hex 颜色 (#RRGGBB) 转换为最接近的 Minecraft § 颜色代码
static char hexToNearestMcColor(const std::string& hex) {
    if (hex.size() < 7 || hex[0] != '#') return 0;
    try {
        int r = std::stoi(hex.substr(1, 2), nullptr, 16);
        int g = std::stoi(hex.substr(3, 2), nullptr, 16);
        int b = std::stoi(hex.substr(5, 2), nullptr, 16);

        static const struct { int r, g, b; char code; } palette[] = {
            {0,0,0, '0'}, {0,0,170, '1'}, {0,170,0, '2'}, {0,170,170, '3'},
            {170,0,0, '4'}, {170,0,170, '5'}, {255,170,0, '6'}, {170,170,170, '7'},
            {85,85,85, '8'}, {85,85,255, '9'}, {85,255,85, 'a'}, {85,255,255, 'b'},
            {255,85,85, 'c'}, {255,85,255, 'd'}, {255,255,85, 'e'}, {255,255,255, 'f'},
        };
        char bestCode = 'f';
        int bestDist = INT32_MAX;
        for (const auto& c : palette) {
            int dr = r - c.r, dg = g - c.g, db = b - c.b;
            int dist = dr*dr + dg*dg + db*db;
            if (dist < bestDist) {
                bestDist = dist;
                bestCode = c.code;
            }
        }
        return bestCode;
    } catch (...) {
        return 0;
    }
}

// 将颜色值（名称或 #hex）转换为 § 代码
static std::string colorToSection(const std::string& color) {
    if (color.empty()) return "";
    char code = 0;
    if (color[0] == '#') {
        code = hexToNearestMcColor(color);
    } else {
        code = colorNameToCode(color);
    }
    if (code) return std::string(1, '\xa7') + code;
    return "";
}

// 递归提取 Minecraft Component JSON 为带 § 颜色代码的纯文本
static std::string extractComponentText(const njson& component) {
    std::string result;

    if (component.is_string()) {
        return component.get<std::string>();
    }

    if (component.is_object()) {
        // 颜色前缀
        if (component.contains("color") && component["color"].is_string()) {
            result += colorToSection(component["color"].get<std::string>());
        }

        // 文本内容
        if (component.contains("text") && component["text"].is_string()) {
            result += component["text"].get<std::string>();
        }

        // 递归处理 extra 或 with 数组
        for (const char* key : {"extra", "with"}) {
            if (component.contains(key) && component[key].is_array()) {
                for (const auto& child : component[key]) {
                    result += extractComponentText(child);
                }
                break; // 只处理第一个匹配的数组
            }
        }
    }

    if (component.is_array()) {
        for (const auto& child : component) {
            result += extractComponentText(child);
        }
    }

    return result;
}

// 提取 MOTD（description 字段）
static std::string extractMotd(const njson& root) {
    if (!root.contains("description")) return "";
    return extractComponentText(root["description"]);
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
        SP_LOGI("Status JSON (%zu bytes): %.500s", jsonStr.size(), jsonStr.c_str());

        // 解析 JSON (nlohmann/json)
        try {
            njson root = njson::parse(jsonStr);

            // MOTD
            result.motd = extractMotd(root);
            SP_LOGI("Parsed MOTD: [%s]", result.motd.c_str());

            // 版本
            if (root.contains("version") && root["version"].is_object()) {
                if (root["version"].contains("name") && root["version"]["name"].is_string())
                    result.versionName = root["version"]["name"].get<std::string>();
                if (root["version"].contains("protocol") && root["version"]["protocol"].is_number())
                    result.protocolVersion = root["version"]["protocol"].get<int>();
            }

            // 玩家
            if (root.contains("players") && root["players"].is_object()) {
                if (root["players"].contains("online") && root["players"]["online"].is_number())
                    result.onlinePlayers = root["players"]["online"].get<int>();
                if (root["players"].contains("max") && root["players"]["max"].is_number())
                    result.maxPlayers = root["players"]["max"].get<int>();
            }

            // 服务器图标
            if (root.contains("favicon") && root["favicon"].is_string()) {
                std::string favicon = root["favicon"].get<std::string>();
                const std::string prefix = "data:image/png;base64,";
                if (favicon.size() > prefix.size() && favicon.substr(0, prefix.size()) == prefix) {
                    result.faviconPng = base64Decode(favicon.substr(prefix.size()));
                    SP_LOGI("Favicon decoded: %zu bytes PNG", result.faviconPng.size());
                }
            }
        } catch (const std::exception& e) {
            SP_LOGE("JSON parse error: %s", e.what());
            close(sock);
            return result;
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
