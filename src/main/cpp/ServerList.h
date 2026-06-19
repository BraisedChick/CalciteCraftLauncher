#pragma once
#include <string>
#include <vector>
#include <cstdint>

struct PingResult {
    bool success = false;
    std::string motd;          // 服务器 MOTD 文本（可能包含 \n 换行）
    std::string versionName;   // 服务器版本名
    int protocolVersion = 0;
    int onlinePlayers = 0;
    int maxPlayers = 0;
    int latencyMs = -1;        // 延迟（毫秒），-1=未测量
    std::vector<uint8_t> faviconPng;  // 服务器图标 PNG 原始数据（空=无图标）
};

class ServerList {
public:
    // 对指定服务器执行 Server List Ping 协议
    // 返回 PingResult，包含 MOTD、人数、延迟、图标数据
    static PingResult ping(const std::string& host, int port, int protocolVersion = 758);

    // base64 解码（用于 favicon）
    static std::vector<uint8_t> base64Decode(const std::string& encoded);
};
