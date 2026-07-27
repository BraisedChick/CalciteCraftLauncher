#pragma once

#include "Screen.h"
#include <GLES3/gl3.h>
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>

// 多人游戏界面（对应 MC 的 JoinMultiplayerScreen + AddServerScreen）
class MultiplayerScreen : public Screen {
public:
    // 后台 ping 线程与渲染线程共享的结果槽：
    // 线程只持有 shared_ptr<PingTask>（不捕获 this），屏幕销毁后线程写入的仍是存活对象
    struct PingTask {
        std::mutex mtx;
        bool done = false;
        bool success = false;
        std::string motd;
        int onlinePlayers = -1;
        int maxPlayers = -1;
        int latencyMs = -1;
        std::vector<uint8_t> faviconPng;
    };

    struct ServerInfo {
        std::string name;
        std::string ip;
        int port = 25565;
        std::string motd;
        int onlinePlayers = -1;
        int maxPlayers = -1;
        int latencyMs = -1;
        GLuint iconTextureID = 0;
        uint64_t iconTexVk = 0;  // Vulkan 后端：VkDescriptorSet 作 ImTextureID（由 VulkanRenderer 缓存管理）
        std::vector<uint8_t> faviconPngData;
        bool pinged = false;
        bool pinging = false;
        bool pingFailed = false;
        std::shared_ptr<PingTask> pingTask;
    };

    using ConnectCallback = std::function<void(const std::string& ip, int port, const std::string& address)>;
    using BackCallback = std::function<void()>;

    void setConnectCallback(ConnectCallback cb) { connectCallback = cb; }
    void setBackCallback(BackCallback cb) { backCallback = cb; }

    const char* getName() const override { return "MultiplayerScreen"; }
    bool isPauseScreen() const override { return true; }
    void init(int width, int height) override;
    void removed() override;
    void render(int mouseX, int mouseY) override;

    // GL 资源重置（EGL context 丢失时）
    void resetGLResources();

private:
    void renderServerList();
    void renderAddServer();
    void connectToServer(const ServerInfo& server);
    void pingAllServers();
    void pingServer(int index);
    void pollPingTasks();
    void loadServerList();
    void saveServerList();

    ConnectCallback connectCallback;
    BackCallback backCallback;

    std::vector<ServerInfo> servers;
    int selectedServer = -1;
    bool showingAddServer = false;
    int editingServerIndex = -1;
    char addServerName[64] = "";
    char addServerIp[64] = "";
    char addServerPort[16] = "25565";

    GLuint defaultServerIconTexID = 0;
    GLuint pingTex[5] = {};
    GLuint pingingTex[5] = {};
    GLuint unreachableTexID = 0;
};
