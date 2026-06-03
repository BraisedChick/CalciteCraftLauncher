#include "GameUI.h"
#include <android/log.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <chrono>

// ImGui 配置：OpenGL ES3 + 自定义加载器（使用 Android NDK 的 GLES3 头文件）
#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "CameraController.h"
#include "Collision.h"
#include "PlayerInventory.h"
#include "ClientEngine.h"
#include "ResourcepackManager.h"
#include "BlockRegistry.h"

#define LOG_TAG "GameUI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 按键码（与 CameraController.cpp 一致）
#define GAMEKEY_W    0
#define GAMEKEY_S    1
#define GAMEKEY_A    2
#define GAMEKEY_D    3
#define GAMEKEY_UP   4
#define GAMEKEY_DOWN 5
#define GAMEKEY_SPRINT 6

// 服务器列表文件路径
#define SERVERS_FILE_PATH "/data/data/com.calcite/servers.txt"

// 游戏内 UI 布局常量
#define JOYSTICK_CENTER_X  220.0f
#define JOYSTICK_CENTER_Y_OFFSET 210.0f  // 距屏幕底部
#define JOYSTICK_RADIUS    110.0f
#define JOYSTICK_KNOB_RADIUS 38.0f
#define JOYSTICK_MAX_DIST  65.0f
#define BTN_RADIUS         38.0f
#define BTN_RIGHT_MARGIN   90.0f
#define BTN_VERTICAL_SPACING 85.0f

GameUI& GameUI::getInstance() {
    static GameUI instance;
    return instance;
}

bool GameUI::init() {
    if (initialized) return true;

    LOGI("Initializing ImGui...");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // 加载支持中文的字体
    static const char* fontPaths[] = {
        "/system/fonts/NotoSansSC-Regular.otf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
    };
    ImFont* font = nullptr;
    for (const char* path : fontPaths) {
        FILE* f = fopen(path, "rb");
        if (f) {
            fclose(f);
            font = io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            if (font) {
                LOGI("Loaded CJK font: %s", path);
                break;
            }
        }
    }
    if (!font) {
        font = io.Fonts->AddFontDefault();
        LOGE("No CJK font found, Chinese text may display as boxes");
    }

    io.FontGlobalScale = 2.0f;

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.FrameBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.35f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.35f, 0.45f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.16f, 1.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("Failed to initialize ImGui OpenGL3 backend");
        return false;
    }

    initialized = true;
    LOGI("ImGui initialized successfully");

    loadServerList();
    LOGI("Loaded %zu servers", servers.size());
    return true;
}

void GameUI::shutdown() {
    if (!initialized) return;
    LOGI("Shutting down ImGui...");

    saveServerList();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    initialized = false;
}

void GameUI::queueTouchEvent(float x, float y, int action) {
    std::lock_guard<std::mutex> lock(touchMutex);
    touchEvents.push_back({x, y, action});
}

void GameUI::processTouchEvents() {
    std::vector<TouchEvent> events;
    {
        std::lock_guard<std::mutex> lock(touchMutex);
        events.swap(touchEvents);
    }

    ImGuiIO& io = ImGui::GetIO();

    for (const auto& e : events) {
        io.AddMousePosEvent(e.x, e.y);
        if (e.action == 0) {
            io.AddMouseButtonEvent(0, true);
        } else if (e.action == 1) {
            io.AddMouseButtonEvent(0, false);
        }
    }
}

void GameUI::addInputCharacter(unsigned int c) {
    if (!initialized) return;
    ImGuiIO& io = ImGui::GetIO();
    if (c == 127 || c == 8) {
        io.AddKeyEvent(ImGuiKey_Backspace, true);
        io.AddKeyEvent(ImGuiKey_Backspace, false);
    } else {
        io.AddInputCharacter(c);
    }
}

bool GameUI::wantsTextInput() {
    if (!initialized) return false;
    return ImGui::GetIO().WantTextInput;
}

void GameUI::render() {
    if (!initialized) return;

    processTouchEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    switch (currentState) {
        case UIState::MAIN_MENU:
            renderMainMenu();
            break;
        case UIState::MULTIPLAYER:
            if (showingAddServer)
                renderAddServer();
            else
                renderMultiplayer();
            break;
        case UIState::CONNECTING:
            renderConnecting();
            break;
        case UIState::IN_GAME:
            renderInGameUI();
            break;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ===== 菜单 UI =====

void GameUI::renderMainMenu() {
    if (optionsOpen) {
        renderGameOptions();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("MainMenu", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetCursorPos(ImVec2(w * 0.5f - 120.0f, h * 0.12f));
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "MINECRAFT");

    float btnW = 280.0f;
    float btnH = 50.0f;
    float startY = h * 0.38f;
    float spacing = 12.0f;

    ImGui::SetCursorPos(ImVec2(w * 0.5f - btnW * 0.5f, startY));
    if (ImGui::Button("多人游戏", ImVec2(btnW, btnH))) {
        currentState = UIState::MULTIPLAYER;
    }

    ImGui::SetCursorPos(ImVec2(w * 0.5f - btnW * 0.5f, startY + (btnH + spacing) * 1));
    if (ImGui::Button("选项", ImVec2(btnW, btnH))) {
        optionsOpen = true;
    }

    ImGui::SetCursorPos(ImVec2(w * 0.5f - btnW * 0.5f, startY + (btnH + spacing) * 2));
    if (ImGui::Button("退出游戏", ImVec2(btnW, btnH))) {
        if (exitCallback) exitCallback();
    }

    ImGui::SetCursorPos(ImVec2(w * 0.5f - 80.0f, h - 40.0f));
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "v1.18.2");

    ImGui::End();
}

void GameUI::renderMultiplayer() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Multiplayer", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // 标题
    ImGui::SetCursorPos(ImVec2(w * 0.5f - 80.0f, 25));
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "多人游戏");

    // 服务器列表 - 可滚动区域（居中，留空白边距）
    float listStartY = 75.0f;
    float listEndY = h - 90.0f;
    float listHeight = listEndY - listStartY;
    float listWidth = w * 0.7f;
    float listLeft = w * 0.5f - listWidth * 0.5f;

    ImGui::SetCursorPos(ImVec2(listLeft, listStartY));
    ImGui::BeginChild("ServerList", ImVec2(listWidth, listHeight), true);

    if (servers.empty()) {
        ImGui::SetCursorPos(ImVec2(w * 0.5f - 120.0f, listHeight * 0.4f));
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "暂无保存的服务器");
    } else {
        if (ImGui::BeginTable("servers", 1,
            ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("info", ImGuiTableColumnFlags_WidthStretch);

            for (size_t i = 0; i < servers.size(); i++) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 56.0f);

                // 服务器信息列（可点击选择 / 双击连接）
                ImGui::TableNextColumn();
                ImGui::PushID((int)i);

                char label[96];
                snprintf(label, sizeof(label), "%s", servers[i].name.c_str());

                bool isSelected = (selectedServer == (int)i);
                if (ImGui::Selectable(label, isSelected,
                    ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(0, 50.0f)))
                {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        connectToServer(servers[i]);
                    } else {
                        selectedServer = (int)i;
                    }
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    ImGui::EndChild();

    // 底部按钮行：连接服务器 + 添加服务器 + 编辑 + 删除 + 返回
    float btnW = 120.0f;
    float btnGap = 10.0f;
    float totalW = btnW * 5 + btnGap * 4;
    float startX = w * 0.5f - totalW * 0.5f;
    float btnY = h - 70.0f;

    // 连接服务器
    bool noSel = (selectedServer < 0);
    ImGui::SetCursorPos(ImVec2(startX, btnY));
    if (noSel) ImGui::BeginDisabled();
    if (ImGui::Button("连接服务器", ImVec2(btnW, 50))) {
        if (selectedServer >= 0 && selectedServer < (int)servers.size()) {
            connectToServer(servers[selectedServer]);
        }
    }
    if (noSel) ImGui::EndDisabled();

    // 添加服务器
    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(startX + (btnW + btnGap), btnY));
    if (ImGui::Button("添加服务器", ImVec2(btnW, 50))) {
        memset(addServerName, 0, sizeof(addServerName));
        memset(addServerIp, 0, sizeof(addServerIp));
        strncpy(addServerPort, "25565", sizeof(addServerPort) - 1);
        addServerPort[sizeof(addServerPort) - 1] = '\0';
        editingServerIndex = -1;
        showingAddServer = true;
    }

    // 编辑服务器
    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(startX + (btnW + btnGap) * 2, btnY));
    if (noSel) ImGui::BeginDisabled();
    if (ImGui::Button("编辑", ImVec2(btnW, 50))) {
        if (selectedServer >= 0 && selectedServer < (int)servers.size()) {
            const auto& s = servers[selectedServer];
            strncpy(addServerName, s.name.c_str(), sizeof(addServerName) - 1);
            addServerName[sizeof(addServerName) - 1] = '\0';
            strncpy(addServerIp, s.ip.c_str(), sizeof(addServerIp) - 1);
            addServerIp[sizeof(addServerIp) - 1] = '\0';
            snprintf(addServerPort, sizeof(addServerPort), "%d", s.port);
            editingServerIndex = selectedServer;
            showingAddServer = true;
        }
    }
    if (noSel) ImGui::EndDisabled();

    // 删除服务器
    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(startX + (btnW + btnGap) * 3, btnY));
    if (noSel) ImGui::BeginDisabled();
    if (ImGui::Button("删除", ImVec2(btnW, 50))) {
        if (selectedServer >= 0 && selectedServer < (int)servers.size()) {
            servers.erase(servers.begin() + selectedServer);
            selectedServer = -1;
            saveServerList();
        }
    }
    if (noSel) ImGui::EndDisabled();

    // 返回
    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(startX + (btnW + btnGap) * 4, btnY));
    if (ImGui::Button("取消", ImVec2(btnW, 50))) {
        currentState = UIState::MAIN_MENU;
    }

    ImGui::End();
}

void GameUI::renderAddServer() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("AddServer", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    // 标题
    float titleW = 200.0f;
    ImGui::SetCursorPos(ImVec2(w * 0.5f - titleW * 0.5f, h * 0.12f));
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "%s", editingServerIndex >= 0 ? "编辑服务器" : "添加服务器");

    // 表单区域（居中）
    ImGui::SetCursorPosX(w * 0.5f - 200.0f);
    ImGui::BeginGroup();

    float inputW = 400.0f;
    ImGui::PushItemWidth(inputW);

    ImGui::Text("名称");
    ImGui::InputText("##name", addServerName, sizeof(addServerName));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f);

    ImGui::Text("地址");
    ImGui::InputText("##ip", addServerIp, sizeof(addServerIp));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 12.0f);

    ImGui::Text("端口");
    ImGui::InputText("##port", addServerPort, sizeof(addServerPort));

    ImGui::PopItemWidth();
    ImGui::EndGroup();

    // 按钮
    float btnWForm = 140.0f;
    float btnGapForm = 30.0f;
    float btnFormY = ImGui::GetCursorPosY() + 20.0f;
    float btnFormStartX = w * 0.5f - (btnWForm * 2 + btnGapForm) * 0.5f;

    ImGui::SetCursorPos(ImVec2(btnFormStartX, btnFormY));
    if (ImGui::Button("保存", ImVec2(btnWForm, 44)) && strlen(addServerName) > 0 && strlen(addServerIp) > 0) {
        ServerInfo server;
        server.name = addServerName;
        server.ip = addServerIp;
        int port = 25565;
        try { port = std::stoi(addServerPort); } catch (...) { port = 25565; }
        if (port <= 0) port = 25565;
        if (port > 65535) port = 25565;
        server.port = port;
        if (editingServerIndex >= 0 && editingServerIndex < (int)servers.size()) {
            servers[editingServerIndex] = server;
            selectedServer = editingServerIndex;
        } else {
            servers.push_back(server);
            selectedServer = (int)servers.size() - 1;
        }
        saveServerList();
        showingAddServer = false;
    }

    ImGui::SetCursorPos(ImVec2(btnFormStartX + btnWForm + btnGapForm, btnFormY));
    if (ImGui::Button("取消", ImVec2(btnWForm, 44))) {
        showingAddServer = false;
    }

    ImGui::End();
}

void GameUI::connectToServer(const ServerInfo& server) {
    if (connectCallback) {
        connectingAddress = server.ip + ":" + std::to_string(server.port);
        currentState = UIState::CONNECTING;
        connectCallback(server.ip, server.port);
    }
}

void GameUI::loadServerList() {
    servers.clear();
    std::ifstream file(SERVERS_FILE_PATH);
    if (!file.is_open()) {
        LOGI("No server list file found, starting fresh");
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string name, ip, portStr;
        if (!std::getline(ss, name, '\t')) continue;
        if (!std::getline(ss, ip, '\t')) continue;
        if (!std::getline(ss, portStr, '\t')) continue;
        int port = 25565;
        try { port = std::stoi(portStr); } catch (...) { port = 25565; }
        servers.push_back({name, ip, port});
    }
    file.close();
}

void GameUI::saveServerList() {
    std::ofstream file(SERVERS_FILE_PATH, std::ios::trunc);
    if (!file.is_open()) {
        LOGE("Failed to save server list");
        return;
    }
    for (const auto& s : servers) {
        file << s.name << '\t' << s.ip << '\t' << s.port << '\n';
    }
    file.close();
    LOGI("Saved %zu servers", servers.size());
}

void GameUI::renderConnecting() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Connecting", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::SetCursorPos(ImVec2(w * 0.5f - 100.0f, h * 0.4f));
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "正在连接...");

    ImGui::SetCursorPos(ImVec2(w * 0.5f - 120.0f, h * 0.4f + 40.0f));
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", connectingAddress.c_str());

    ImGui::End();
}

// ===== 游戏内 UI =====

void GameUI::renderInGameUI() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    // 检测死亡：生命值从 > 0 降到 <= 0 时激活死亡界面
    {
        auto* engine = ClientEngine::getInstance();
        float health = engine ? engine->getHealth() : 20.0f;
        if (!deathScreenActive && health <= 0.0f && prevHealth > 0.0f && engine && engine->getGameMode() != 3) {
            deathScreenActive = true;
            if (deathReason.empty()) {
                std::string serverMsg = engine->getDeathMessage();
                deathReason = serverMsg.empty() ? "你被杀死了" : serverMsg;
            }
        }
        prevHealth = health;
    }

    if (deathScreenActive) {
        renderDeathScreen();
        return;
    }

    if (optionsOpen) {
        renderGameOptions();
        return;
    }
    if (gameMenuOpen) {
        renderInGameMenu();
        return;
    }

    // 使用背景绘制列表（在所有 ImGui 窗口之下）
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    // 摇杆底座（左下角）
    float jx = JOYSTICK_CENTER_X;
    float jy = h - JOYSTICK_CENTER_Y_OFFSET;

    draw->AddCircleFilled(ImVec2(jx, jy), JOYSTICK_RADIUS, IM_COL32(255, 255, 255, 30));
    draw->AddCircle(ImVec2(jx, jy), JOYSTICK_RADIUS, IM_COL32(255, 255, 255, 60));

    // 摇杆摇柄
    float knobX = jx + joystick.knobX;
    float knobY = jy + joystick.knobY;
    draw->AddCircleFilled(ImVec2(knobX, knobY), JOYSTICK_KNOB_RADIUS, IM_COL32(255, 255, 255, 100));
    draw->AddCircle(ImVec2(knobX, knobY), JOYSTICK_KNOB_RADIUS, IM_COL32(255, 255, 255, 160));

    // 上升/下降按钮（右侧居中）
    float btnX = w - BTN_RIGHT_MARGIN;
    float btnUpY = h * 0.5f - BTN_VERTICAL_SPACING;
    float btnDownY = h * 0.5f;

    // 上升按钮
    ImU32 upCol = buttons.upPressed ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 60);
    draw->AddCircleFilled(ImVec2(btnX, btnUpY), BTN_RADIUS, upCol);
    draw->AddCircle(ImVec2(btnX, btnUpY), BTN_RADIUS, IM_COL32(255, 255, 255, 100));
    // 上箭头 △
    draw->AddTriangleFilled(
        ImVec2(btnX, btnUpY - 10),
        ImVec2(btnX - 10, btnUpY + 6),
        ImVec2(btnX + 10, btnUpY + 6),
        IM_COL32(255, 255, 255, 180));

    // 下降按钮
    ImU32 downCol = buttons.downPressed ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 60);
    draw->AddCircleFilled(ImVec2(btnX, btnDownY), BTN_RADIUS, downCol);
    draw->AddCircle(ImVec2(btnX, btnDownY), BTN_RADIUS, IM_COL32(255, 255, 255, 100));
    // 下箭头 ▽
    draw->AddTriangleFilled(
        ImVec2(btnX, btnDownY + 10),
        ImVec2(btnX - 10, btnDownY - 6),
        ImVec2(btnX + 10, btnDownY - 6),
        IM_COL32(255, 255, 255, 180));

    // 疾跑按钮（下降按钮下方）
    float btnSprintY = h * 0.5f + BTN_VERTICAL_SPACING;
    ImU32 sprintCol = buttons.sprintPressed ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 60);
    draw->AddCircleFilled(ImVec2(btnX, btnSprintY), BTN_RADIUS, sprintCol);
    draw->AddCircle(ImVec2(btnX, btnSprintY), BTN_RADIUS, IM_COL32(255, 255, 255, 100));
    // 疾跑图标：省略号 + 箭头 → 简化为三条斜线
    draw->AddLine(ImVec2(btnX - 8, btnSprintY - 6), ImVec2(btnX + 8, btnSprintY + 2), IM_COL32(255, 255, 255, 180), 3.0f);
    draw->AddLine(ImVec2(btnX - 8, btnSprintY), ImVec2(btnX + 8, btnSprintY + 6), IM_COL32(255, 255, 255, 180), 3.0f);
    draw->AddLine(ImVec2(btnX - 8, btnSprintY + 6), ImVec2(btnX + 8, btnSprintY + 10), IM_COL32(255, 255, 255, 180), 3.0f);

    // ===== F3 按钮（屏幕最上方靠左四分之一处） =====
    {
        const float F3_W = 52.0f;
        const float F3_H = 28.0f;
        const float F3_X = w * 0.25f - F3_W * 0.5f;
        const float F3_Y = 10.0f;
        ImU32 f3Col = showDebugInfo ? IM_COL32(255, 255, 0, 200) : IM_COL32(255, 255, 255, 80);
        ImU32 f3Bg = showDebugInfo ? IM_COL32(255, 255, 0, 40) : IM_COL32(255, 255, 255, 25);
        draw->AddRectFilled(ImVec2(F3_X, F3_Y), ImVec2(F3_X + F3_W, F3_Y + F3_H), f3Bg, 6.0f);
        draw->AddRect(ImVec2(F3_X, F3_Y), ImVec2(F3_X + F3_W, F3_Y + F3_H), f3Col, 6.0f, 0, 1.5f);
        const char* f3Text = "F3";
        ImVec2 textSize = ImGui::CalcTextSize(f3Text);
        float textX = F3_X + (F3_W - textSize.x) * 0.5f;
        float textY = F3_Y + (F3_H - textSize.y) * 0.5f;
        draw->AddText(ImVec2(textX, textY), f3Col, f3Text);
    }

    // ===== 准星（屏幕中央） =====
    float cx = w * 0.5f;
    float cy = h * 0.5f;
    const float CROSS_SIZE = 16.0f;
    const float CROSS_GAP = 0.0f;
    const float CROSS_THICK = 3.0f;
    ImU32 crossCol = IM_COL32(255, 255, 255, 200);
    draw->AddLine(ImVec2(cx - CROSS_SIZE, cy), ImVec2(cx - CROSS_GAP, cy), crossCol, CROSS_THICK);
    draw->AddLine(ImVec2(cx + CROSS_GAP, cy), ImVec2(cx + CROSS_SIZE, cy), crossCol, CROSS_THICK);
    draw->AddLine(ImVec2(cx, cy - CROSS_SIZE), ImVec2(cx, cy - CROSS_GAP), crossCol, CROSS_THICK);
    draw->AddLine(ImVec2(cx, cy + CROSS_GAP), ImVec2(cx, cy + CROSS_SIZE), crossCol, CROSS_THICK);

    // ===== 快捷栏 =====
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Hotbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoBackground);

    const float SLOT_SIZE = 55.0f;
    const float SLOT_GAP = 5.0f;
    const float HOTBAR_Y = h - 61.0f;
    float totalW = 9.0f * SLOT_SIZE + 8.0f * SLOT_GAP;
    float hotbarX = w * 0.5f - totalW * 0.5f;

    // 背景半透明条
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(hotbarX - 6, HOTBAR_Y - 6),
        ImVec2(hotbarX + totalW + 6, HOTBAR_Y + SLOT_SIZE + 6),
        IM_COL32(0, 0, 0, 100), 4.0f);

    InvSlot hotbar[9];
    PlayerInventory::getInstance().getHotbarSlots(hotbar);
    int selSlot = PlayerInventory::getInstance().getSelectedSlot();

    for (int i = 0; i < 9; i++) {
        float sx = hotbarX + i * (SLOT_SIZE + SLOT_GAP);
        // 格子背景
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(sx, HOTBAR_Y),
            ImVec2(sx + SLOT_SIZE, HOTBAR_Y + SLOT_SIZE),
            IM_COL32(40, 40, 50, 200), 2.0f);

        // 选中高亮
        if (i == selSlot) {
            ImGui::GetWindowDrawList()->AddRect(
                ImVec2(sx - 2, HOTBAR_Y - 2),
                ImVec2(sx + SLOT_SIZE + 2, HOTBAR_Y + SLOT_SIZE + 2),
                IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.5f);
        }

        if (hotbar[i].present && hotbar[i].itemId > 0) {
            std::string itemName = BlockRegistry::getInstance().getItemName(hotbar[i].itemId);
            if (itemName.empty()) {
                // 物品ID在 items.json 中找不到对应的名字
                LOGI("Hotbar[%d]: itemId=%d has no name mapping!", i, hotbar[i].itemId);
            } else {
                GLuint tex = ResourcepackManager::getInstance().getItemTexture(itemName);
                if (tex != 0) {
                    // ImGui 的字体纹理使用 GL_LINEAR sampler，会泄漏到后续绘制
                    // 解除 sampler 绑定，让物品纹理使用自己的 GL_NEAREST 过滤
                    ImGui::GetWindowDrawList()->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
                        glBindSampler(0, 0);
                    }, nullptr);
                    float pad = 5.0f;
                    float iconSize = SLOT_SIZE - pad * 2;
                    ImGui::SetCursorScreenPos(ImVec2((int)(sx + pad), (int)(HOTBAR_Y + pad)));
                    ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2(iconSize, iconSize));
                }
            }

            // 物品数量
            if (hotbar[i].count > 1) {
                char countStr[8];
                snprintf(countStr, sizeof(countStr), "%d", hotbar[i].count);
                ImVec2 textSize = ImGui::CalcTextSize(countStr);
                ImGui::SetCursorScreenPos(ImVec2(
                    sx + SLOT_SIZE - textSize.x - 4,
                    HOTBAR_Y + SLOT_SIZE - textSize.y - 2));
                ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", countStr);
            }
        }
    }

    // ===== 生命值 + 饥饿值（创造模式不显示） =====
    {
        auto* engine = ClientEngine::getInstance();
        if (engine && engine->getGameMode() != 1) {
            float healthVal = engine->getHealth();
            int foodVal = engine->getFood();
            const float ICON_SIZE = 20.0f;
            const float GAP = 1.0f;
            const float HUD_Y = h - 61.0f - ICON_SIZE - 8.0f;  // 快捷栏上方

            GLuint hContainer = ResourcepackManager::getInstance().getHudTexture("heart/container");
            GLuint hFull = ResourcepackManager::getInstance().getHudTexture("heart/full");
            GLuint hHalf = ResourcepackManager::getInstance().getHudTexture("heart/half");

            for (int i = 0; i < 10; i++) {
                float hx = hotbarX + i * (ICON_SIZE + GAP);
                if (hContainer) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)hContainer,
                        ImVec2(hx, HUD_Y), ImVec2(hx + ICON_SIZE, HUD_Y + ICON_SIZE));
                }
                float remain = healthVal - i * 2.0f;
                if (remain >= 2.0f && hFull) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)hFull,
                        ImVec2(hx, HUD_Y), ImVec2(hx + ICON_SIZE, HUD_Y + ICON_SIZE));
                } else if (remain >= 1.0f && hHalf) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)hHalf,
                        ImVec2(hx, HUD_Y), ImVec2(hx + ICON_SIZE, HUD_Y + ICON_SIZE));
                }
            }

            // ===== 饥饿值（顶部右侧） =====
            GLuint fEmpty = ResourcepackManager::getInstance().getHudTexture("food_empty");
            GLuint fFull = ResourcepackManager::getInstance().getHudTexture("food_full");
            GLuint fHalf = ResourcepackManager::getInstance().getHudTexture("food_half");

            float totalWFood = 10.0f * (ICON_SIZE + GAP) - GAP;
            float foodStartX = hotbarX + totalW - totalWFood;

            for (int i = 0; i < 10; i++) {
                float fx = foodStartX + i * (ICON_SIZE + GAP);
                if (fEmpty) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)fEmpty,
                        ImVec2(fx, HUD_Y), ImVec2(fx + ICON_SIZE, HUD_Y + ICON_SIZE));
                }
                int remain = foodVal - i * 2;
                if (remain >= 2 && fFull) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)fFull,
                        ImVec2(fx, HUD_Y), ImVec2(fx + ICON_SIZE, HUD_Y + ICON_SIZE));
                } else if (remain >= 1 && fHalf) {
                    ImGui::GetWindowDrawList()->AddImage(
                        (ImTextureID)(intptr_t)fHalf,
                        ImVec2(fx, HUD_Y), ImVec2(fx + ICON_SIZE, HUD_Y + ICON_SIZE));
                }
            }
        }
    }

    // ===== E 按钮（物品栏右侧） =====
    {
        float eX = hotbarX + totalW + 10.0f;
        ImU32 eCol = inventoryOpen ? IM_COL32(255, 255, 0, 200) : IM_COL32(255, 255, 255, 180);
        ImU32 eBg = inventoryOpen ? IM_COL32(255, 255, 0, 40) : IM_COL32(40, 40, 50, 180);
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(eX, HOTBAR_Y), ImVec2(eX + SLOT_SIZE, HOTBAR_Y + SLOT_SIZE),
            eBg, 4.0f);
        ImGui::GetWindowDrawList()->AddRect(
            ImVec2(eX, HOTBAR_Y), ImVec2(eX + SLOT_SIZE, HOTBAR_Y + SLOT_SIZE),
            eCol, 4.0f, 0, 2.0f);
        const char* eText = "E";
        ImVec2 eTextSize = ImGui::CalcTextSize(eText);
        float eTextX = eX + (SLOT_SIZE - eTextSize.x) * 0.5f;
        float eTextY = HOTBAR_Y + (SLOT_SIZE - eTextSize.y) * 0.5f;
        ImGui::GetWindowDrawList()->AddText(ImVec2(eTextX, eTextY), eCol, eText);
    }

    ImGui::End();

    // ===== F3 调试信息 =====
    // ===== 背包界面 =====
    if (inventoryOpen) {
        renderInventory();
    }

    if (showDebugInfo) {
        // 手动计算 FPS（ImGui::GetIO().Framerate 在混合帧率下不准）
        static auto lastFpsTime = std::chrono::steady_clock::now();
        static int fpsCounter = 0;
        static float displayFps = 0.0f;
        fpsCounter++;
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastFpsTime).count();
        if (elapsed >= 1.0f) {
            displayFps = fpsCounter / elapsed;
            fpsCounter = 0;
            lastFpsTime = now;
        }

        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::Begin("DebugInfo", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_NoBackground);

        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Minecraft 1.18.2");
        ImGui::Text("FPS: %.0f", displayFps);
        ImGui::Text("");

        auto pos = CameraController::getInstance().getPosition();
        ImGui::Text("XYZ: %.1f / %.1f / %.1f", pos.x, pos.y, pos.z);

        ImGui::End();
    }
}

void GameUI::renderInventory() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    const float INV_SLOT = 50.0f;

    // 纹理坐标 → 屏幕坐标缩放
    const float TEX_SLOT = 18.0f;
    const float TEX_LEFT = 7.0f;
    const float TEX_TOP = 83.0f;
    const float TEX_HOTBAR = 141.0f;
    const float TEX_CONTAINER_W = 176.0f;
    const float TEX_CONTAINER_H = 166.0f;
    float S = INV_SLOT / TEX_SLOT;

    // 容器居中
    float containerW = TEX_CONTAINER_W * S;
    float containerH = TEX_CONTAINER_H * S;
    float containerX = (w - containerW) * 0.5f;
    float containerY = h * 0.5f - containerH * 0.5f;

    // 格子的起始坐标（由纹理位置决定）
    float gridX = containerX + TEX_LEFT * S;
    float gridY = containerY + TEX_TOP * S;
    float hotbarY = containerY + TEX_HOTBAR * S;

    // 背景遮罩
    ImGui::GetForegroundDrawList()->AddRectFilled(
        ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 160));

    // 容器纹理
    GLuint bgTex = ResourcepackManager::getInstance().getGuiTexture("container/inventory");
    if (bgTex != 0) {
        ImGui::GetForegroundDrawList()->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
            glBindSampler(0, 0);
        }, nullptr);
        ImGui::GetForegroundDrawList()->AddImage(
            (ImTextureID)(intptr_t)bgTex,
            ImVec2(containerX, containerY),
            ImVec2(containerX + containerW, containerY + containerH),
            ImVec2(0, 0),
            ImVec2(TEX_CONTAINER_W / 256.0f, TEX_CONTAINER_H / 256.0f));
    }

    // 标题
    const char* title = "背包";
    ImVec2 titleSize = ImGui::CalcTextSize(title);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2(containerX + (containerW - titleSize.x) * 0.5f,
               containerY + 8.0f * S),
        IM_COL32(55, 55, 55, 255), title);

    // 获取物品栏数据
    auto& inv = PlayerInventory::getInstance();
    InvSlot hotbar[9];
    inv.getHotbarSlots(hotbar);

    // 渲染格子中的物品（纹理自带格子背景，我们只需叠加物品）
    auto renderItem = [&](float sx, float sy, const InvSlot& slot) {
        if (!slot.present || slot.itemId <= 0) return;

        std::string itemName = BlockRegistry::getInstance().getItemName(slot.itemId);
        if (itemName.empty()) return;

        GLuint tex = ResourcepackManager::getInstance().getItemTexture(itemName);
        if (tex == 0) return;

        ImGui::GetForegroundDrawList()->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
            glBindSampler(0, 0);
        }, nullptr);

        float pad = 5.0f;
        float iconSize = INV_SLOT - pad * 2;
        ImGui::GetForegroundDrawList()->AddImage(
            (ImTextureID)(intptr_t)tex,
            ImVec2(sx + pad, sy + pad),
            ImVec2(sx + pad + iconSize, sy + pad + iconSize));

        if (slot.count > 1) {
            char countStr[8];
            snprintf(countStr, sizeof(countStr), "%d", slot.count);
            ImVec2 textSize = ImGui::CalcTextSize(countStr);
            ImGui::GetForegroundDrawList()->AddText(
                ImVec2(sx + INV_SLOT - textSize.x - 3,
                       sy + INV_SLOT - textSize.y - 2),
                IM_COL32(255, 255, 255, 255), countStr);
        }
    };

    // 主背包格（3行 x 9列）
    for (int row = 0; row < 3; row++) {
        float rowY = gridY + row * INV_SLOT;
        for (int col = 0; col < 9; col++) {
            float sx = gridX + col * INV_SLOT;
            int slotIndex = row * 9 + col;
            renderItem(sx, rowY, inv.getMainSlot(slotIndex));
        }
    }

    // 快捷栏（1行 x 9列）
    for (int i = 0; i < 9; i++) {
        float sx = gridX + i * INV_SLOT;

        // 选中高亮
        if (i == inv.getSelectedSlot()) {
            ImGui::GetForegroundDrawList()->AddRect(
                ImVec2(sx - 2, hotbarY - 2),
                ImVec2(sx + INV_SLOT + 2, hotbarY + INV_SLOT + 2),
                IM_COL32(255, 255, 255, 255), 3.0f, 0, 2.5f);
        }

        renderItem(sx, hotbarY, hotbar[i]);
    }
}

void GameUI::renderDeathScreen() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    // 半透明红色遮罩
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(80, 0, 0, 160));

    // 死亡窗口
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("DeathScreen", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);

    const float BTN_W = 360.0f;
    const float BTN_H = 55.0f;
    const float SPACING = 20.0f;
    const float CENTER_X = w * 0.5f;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(100, 30, 30, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(140, 50, 50, 240));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(160, 70, 70, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));

    // 第一行：你死了！
    float textW = ImGui::CalcTextSize("你死了！").x;
    ImGui::SetCursorPos(ImVec2(CENTER_X - textW * 0.5f, h * 0.3f));
    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "你死了！");

    // 第二行：死亡原因
    textW = ImGui::CalcTextSize(deathReason.c_str()).x;
    ImGui::SetCursorPos(ImVec2(CENTER_X - textW * 0.5f, h * 0.3f + 45.0f));
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", deathReason.c_str());

    // 重生按钮
    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, h * 0.5f));
    if (ImGui::Button("重生", ImVec2(BTN_W, BTN_H))) {
        deathScreenActive = false;
        deathReason.clear();
        auto* engine = ClientEngine::getInstance();
        if (engine) {
            engine->clearDeathMessage();
            engine->sendRespawn();
        }
    }

    // 标题屏幕按钮
    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, h * 0.5f + BTN_H + SPACING));
    if (ImGui::Button("标题屏幕", ImVec2(BTN_W, BTN_H))) {
        deathScreenActive = false;
        deathReason.clear();
        if (auto* engine = ClientEngine::getInstance()) {
            engine->clearDeathMessage();
        }
        if (disconnectCallback) {
            disconnectCallback();
        }
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::End();
}

void GameUI::renderInGameMenu() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    // 半透明黑色遮罩
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 180));

    // 游戏菜单窗口（包含可点击按钮）
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("GameMenu", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);

    const float BTN_W = 220.0f;
    const float BTN_H = 50.0f;
    const float SPACING = 20.0f;
    const float CENTER_X = w * 0.5f;
    float startY = h * 0.5f - BTN_H * 1.5f - SPACING;

    // 按钮样式
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 50, 50, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 80, 80, 240));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(100, 100, 100, 255));

    // 回到游戏
    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, startY));
    if (ImGui::Button("回到游戏", ImVec2(BTN_W, BTN_H))) {
        gameMenuOpen = false;
    }

    // 选项
    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, startY + BTN_H + SPACING));
    if (ImGui::Button("选项", ImVec2(BTN_W, BTN_H))) {
        optionsOpen = true;
    }

    // 断开连接
    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, startY + (BTN_H + SPACING) * 2));
    if (ImGui::Button("断开连接", ImVec2(BTN_W, BTN_H))) {
        gameMenuOpen = false;
        if (disconnectCallback) {
            disconnectCallback();
        }
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::End();
}

void GameUI::renderGameOptions() {
    if (videoSettingsOpen) {
        renderVideoSettings();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    // 半透明黑色遮罩
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 180));

    // 选项窗口
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("GameOptions", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);

    const float PANEL_W = 300.0f;
    const float PANEL_H = 260.0f;
    float panelX = w * 0.5f - PANEL_W * 0.5f;
    float panelY = h * 0.5f - PANEL_H * 0.5f;

    // 面板背景
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(panelX, panelY),
        ImVec2(panelX + PANEL_W, panelY + PANEL_H),
        IM_COL32(40, 40, 50, 220), 8.0f);
    ImGui::GetWindowDrawList()->AddRect(
        ImVec2(panelX, panelY),
        ImVec2(panelX + PANEL_W, panelY + PANEL_H),
        IM_COL32(100, 100, 120, 255), 8.0f);

    // FOV 滑块
    ImGui::SetCursorPos(ImVec2(panelX + 20, panelY + 30));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(60, 60, 70, 200));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(120, 180, 255, 220));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(150, 200, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));

    ImGui::SetNextItemWidth(PANEL_W - 40);
    if (ImGui::SliderFloat("##fov", &optionsFov, 30.0f, 120.0f, "视场角: %.0f°")) {
        if (fovCallback) fovCallback(optionsFov);
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    // 视频设置按钮
    ImGui::SetCursorPos(ImVec2(panelX + PANEL_W * 0.5f - 100.0f, panelY + 95));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 50, 50, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 80, 80, 240));
    if (ImGui::Button("视频设置", ImVec2(200, 40))) {
        videoSettingsOpen = true;
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    // 完成按钮
    ImGui::SetCursorPos(ImVec2(panelX + PANEL_W * 0.5f - 60.0f, panelY + PANEL_H - 50));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 50, 50, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 80, 80, 240));
    if (ImGui::Button("完成", ImVec2(120, 40))) {
        optionsOpen = false;
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGui::End();
}

void GameUI::renderVideoSettings() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    // 半透明黑色遮罩
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(w, h), IM_COL32(0, 0, 0, 180));

    // 视频设置窗口
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("VideoSettings", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);

    const float PANEL_W = 300.0f;
    const float PANEL_H = 200.0f;
    float panelX = w * 0.5f - PANEL_W * 0.5f;
    float panelY = h * 0.5f - PANEL_H * 0.5f;

    // 面板背景
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(panelX, panelY),
        ImVec2(panelX + PANEL_W, panelY + PANEL_H),
        IM_COL32(40, 40, 50, 220), 8.0f);
    ImGui::GetWindowDrawList()->AddRect(
        ImVec2(panelX, panelY),
        ImVec2(panelX + PANEL_W, panelY + PANEL_H),
        IM_COL32(100, 100, 120, 255), 8.0f);

    // 渲染距离滑块
    ImGui::SetCursorPos(ImVec2(panelX + 20, panelY + 40));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(60, 60, 70, 200));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(120, 180, 255, 220));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(150, 200, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));

    ImGui::SetNextItemWidth(PANEL_W - 40);
    int rd = renderDistance;
    if (ImGui::SliderInt("##renderDist", &rd, 2, 20, "渲染距离: %d 区块")) {
        renderDistance = rd;
        if (renderDistanceCallback) renderDistanceCallback(renderDistance);
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    // 完成按钮
    ImGui::SetCursorPos(ImVec2(panelX + PANEL_W * 0.5f - 60.0f, panelY + PANEL_H - 55));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(50, 50, 50, 220));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(80, 80, 80, 240));
    if (ImGui::Button("完成", ImVec2(120, 40))) {
        videoSettingsOpen = false;
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();

    ImGui::End();
}

bool GameUI::isInJoystickArea(float x, float y) const {
    ImGuiIO& io = ImGui::GetIO();
    float jx = JOYSTICK_CENTER_X;
    float jy = io.DisplaySize.y - JOYSTICK_CENTER_Y_OFFSET;
    float dx = x - jx;
    float dy = y - jy;
    return (dx * dx + dy * dy) <= (JOYSTICK_RADIUS * JOYSTICK_RADIUS * 2.25f); // 1.5x radius
}

bool GameUI::isInUpButtonArea(float x, float y) const {
    ImGuiIO& io = ImGui::GetIO();
    float bx = io.DisplaySize.x - BTN_RIGHT_MARGIN;
    float by = io.DisplaySize.y * 0.5f - BTN_VERTICAL_SPACING;
    float dx = x - bx;
    float dy = y - by;
    return (dx * dx + dy * dy) <= (BTN_RADIUS * BTN_RADIUS * 1.5f);
}

bool GameUI::isInDownButtonArea(float x, float y) const {
    ImGuiIO& io = ImGui::GetIO();
    float bx = io.DisplaySize.x - BTN_RIGHT_MARGIN;
    float by = io.DisplaySize.y * 0.5f;
    float dx = x - bx;
    float dy = y - by;
    return (dx * dx + dy * dy) <= (BTN_RADIUS * BTN_RADIUS * 1.5f);
}

bool GameUI::isInSprintButtonArea(float x, float y) const {
    ImGuiIO& io = ImGui::GetIO();
    float bx = io.DisplaySize.x - BTN_RIGHT_MARGIN;
    float by = io.DisplaySize.y * 0.5f + BTN_VERTICAL_SPACING;
    float dx = x - bx;
    float dy = y - by;
    return (dx * dx + dy * dy) <= (BTN_RADIUS * BTN_RADIUS * 1.5f);
}

bool GameUI::isInF3ButtonArea(float x, float y) const {
    ImGuiIO& io = ImGui::GetIO();
    const float F3_W = 52.0f;
    const float F3_H = 28.0f;
    const float F3_X = io.DisplaySize.x * 0.25f - F3_W * 0.5f;
    const float F3_Y = 10.0f;
    return x >= F3_X && x <= F3_X + F3_W && y >= F3_Y && y <= F3_Y + F3_H;
}

bool GameUI::isInEButtonArea(float x, float y) const {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;
    const float SLOT_SIZE = 55.0f;
    const float SLOT_GAP = 5.0f;
    const float HOTBAR_Y = h - 61.0f;
    float totalW = 9.0f * SLOT_SIZE + 8.0f * SLOT_GAP;
    float hotbarX = w * 0.5f - totalW * 0.5f;
    float btnX = hotbarX + totalW + 10.0f;
    return x >= btnX && x <= btnX + SLOT_SIZE && y >= HOTBAR_Y && y <= HOTBAR_Y + SLOT_SIZE;
}

int GameUI::hotbarSlotAt(float x, float y) const {
    ImGuiIO& io = ImGui::GetIO();
    float h = io.DisplaySize.y;
    float w = io.DisplaySize.x;
    const float SLOT_SIZE = 55.0f;
    const float SLOT_GAP = 5.0f;
    const float HOTBAR_Y = h - 61.0f;
    float totalW = 9.0f * SLOT_SIZE + 8.0f * SLOT_GAP;
    float hotbarX = w * 0.5f - totalW * 0.5f;

    // 检查是否在快捷栏垂直范围
    if (y < HOTBAR_Y || y > HOTBAR_Y + SLOT_SIZE) return -1;

    // 找出点中的格子
    float relX = x - hotbarX;
    float slotStep = SLOT_SIZE + SLOT_GAP;
    int slot = (int)(relX / slotStep);
    if (slot < 0 || slot > 8) return -1;

    float slotX = hotbarX + slot * slotStep;
    if (x < slotX || x > slotX + SLOT_SIZE) return -1;

    return slot;
}

GameUI::TouchPoint* GameUI::findTouchPoint(int id) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (touchPoints[i].active && touchPoints[i].id == id)
            return &touchPoints[i];
    }
    return nullptr;
}

GameUI::TouchPoint* GameUI::allocTouchPoint(int id) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (!touchPoints[i].active) {
            touchPoints[i].active = true;
            touchPoints[i].id = id;
            touchPoints[i].role = TouchPoint::NONE;
            return &touchPoints[i];
        }
    }
    return nullptr;
}

void GameUI::freeTouchPoint(int id) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (touchPoints[i].active && touchPoints[i].id == id) {
            touchPoints[i].active = false;
            touchPoints[i].role = TouchPoint::NONE;
            return;
        }
    }
}

bool GameUI::isRoleTaken(TouchPoint::Role role) const {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (touchPoints[i].active && touchPoints[i].role == role)
            return true;
    }
    return false;
}

// ===== 多点触控入口 =====

void GameUI::onTouchEvent(int pointerId, float x, float y, int action) {
    // 游戏内菜单、选项、死亡界面打开时，触摸事件交给 ImGui 处理
    if (currentState == UIState::IN_GAME && (gameMenuOpen || optionsOpen || deathScreenActive)) {
        queueTouchEvent(x, y, action);
        return;
    }
    // 背包打开时，触摸事件交给 ImGui，但 E 按钮区域例外（用来关闭背包）
    if (currentState == UIState::IN_GAME && inventoryOpen) {
        if (isInEButtonArea(x, y) && action == 0) {
            inventoryOpen = false;
            return;
        }
        queueTouchEvent(x, y, action);
        return;
    }

    if (action == 0) {
        // DOWN：分配触摸点，根据位置分配角色
        auto* pt = allocTouchPoint(pointerId);
        if (!pt) return;

        if (!isRoleTaken(TouchPoint::JOYSTICK) && isInJoystickArea(x, y)) {
            pt->role = TouchPoint::JOYSTICK;
            handleJoystickTouch(pointerId, x, y, action);
        } else if (!isRoleTaken(TouchPoint::UP_BUTTON) && isInUpButtonArea(x, y)) {
            pt->role = TouchPoint::UP_BUTTON;
            Collision::getInstance().setKeyState(GAMEKEY_UP, true);
            buttons.upPressed = true;
        } else if (!isRoleTaken(TouchPoint::DOWN_BUTTON) && isInDownButtonArea(x, y)) {
            pt->role = TouchPoint::DOWN_BUTTON;
            Collision::getInstance().setKeyState(GAMEKEY_DOWN, true);
            buttons.downPressed = true;
        } else if (!isRoleTaken(TouchPoint::SPRINT_BUTTON) && isInSprintButtonArea(x, y)) {
            pt->role = TouchPoint::SPRINT_BUTTON;
            Collision::getInstance().setKeyState(GAMEKEY_SPRINT, true);
            buttons.sprintPressed = !buttons.sprintPressed;  // 切换视觉状态
        } else if (!isRoleTaken(TouchPoint::F3_BUTTON) && isInF3ButtonArea(x, y)) {
            pt->role = TouchPoint::F3_BUTTON;
            toggleDebugInfo();
        } else if (!isRoleTaken(TouchPoint::E_BUTTON) && isInEButtonArea(x, y)) {
            pt->role = TouchPoint::E_BUTTON;
            inventoryOpen = !inventoryOpen;
        } else if (currentState == UIState::IN_GAME) {
            int hbSlot = hotbarSlotAt(x, y);
            if (hbSlot >= 0) {
                // 点击快捷栏格子：选中该槽位并通知服务器
                PlayerInventory::getInstance().setSelectedSlot(hbSlot);
                ClientEngine::getInstance()->sendHeldItemChange(hbSlot);
            } else {
                pt->role = TouchPoint::CAMERA;
                handleCameraTouch(pointerId, x, y, action);
            }
        } else {
            pt->role = TouchPoint::CAMERA;
            handleCameraTouch(pointerId, x, y, action);
        }
    } else if (action == 2) {
        // MOVE：按角色处理
        auto* pt = findTouchPoint(pointerId);
        if (!pt || !pt->active) return;

        switch (pt->role) {
            case TouchPoint::JOYSTICK:
                handleJoystickTouch(pointerId, x, y, action);
                break;
            case TouchPoint::CAMERA:
                handleCameraTouch(pointerId, x, y, action);
                break;
            default:
                break;
        }
    } else if (action == 1) {
        // UP：释放触摸点和角色
        auto* pt = findTouchPoint(pointerId);
        if (!pt) return;

        switch (pt->role) {
            case TouchPoint::JOYSTICK:
                handleJoystickTouch(pointerId, x, y, action);
                break;
            case TouchPoint::CAMERA:
                break;
            case TouchPoint::UP_BUTTON:
                Collision::getInstance().setKeyState(GAMEKEY_UP, false);
                buttons.upPressed = false;
                break;
            case TouchPoint::DOWN_BUTTON:
                Collision::getInstance().setKeyState(GAMEKEY_DOWN, false);
                buttons.downPressed = false;
                break;
            case TouchPoint::SPRINT_BUTTON:
                break;
            default:
                break;
        }
        freeTouchPoint(pointerId);
    }
}

void GameUI::handleJoystickTouch(int pointerId, float x, float y, int action) {
    ImGuiIO& io = ImGui::GetIO();
    float jx = JOYSTICK_CENTER_X;
    float jy = io.DisplaySize.y - JOYSTICK_CENTER_Y_OFFSET;

    switch (action) {
        case 0: { // DOWN
            joystick.active = true;
            float dx = x - jx;
            float dy = y - jy;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > JOYSTICK_MAX_DIST) {
                dx = dx / dist * JOYSTICK_MAX_DIST;
                dy = dy / dist * JOYSTICK_MAX_DIST;
            }
            joystick.knobX = dx;
            joystick.knobY = dy;
            Collision::getInstance().setJoystickInput(dx / JOYSTICK_MAX_DIST, dy / JOYSTICK_MAX_DIST);
            break;
        }
        case 2: { // MOVE
            if (joystick.active) {
                float dx = x - jx;
                float dy = y - jy;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist > JOYSTICK_MAX_DIST) {
                    dx = dx / dist * JOYSTICK_MAX_DIST;
                    dy = dy / dist * JOYSTICK_MAX_DIST;
                }
                joystick.knobX = dx;
                joystick.knobY = dy;
                Collision::getInstance().setJoystickInput(dx / JOYSTICK_MAX_DIST, dy / JOYSTICK_MAX_DIST);
            }
            break;
        }
        case 1: // UP
        case 3: // CANCEL
            joystick.active = false;
            joystick.knobX = 0;
            joystick.knobY = 0;
            Collision::getInstance().setJoystickInput(0, 0);
            break;
    }
}

void GameUI::handleCameraTouch(int pointerId, float x, float y, int action) {
    auto* pt = findTouchPoint(pointerId);
    if (!pt) return;

    switch (action) {
        case 0: // DOWN
            pt->cameraLastX = x;
            pt->cameraLastY = y;
            break;
        case 2: { // MOVE
            float dx = x - pt->cameraLastX;
            float dy = y - pt->cameraLastY;
            CameraController::getInstance().updateRotation(dy * 0.005f, dx * 0.005f);
            pt->cameraLastX = x;
            pt->cameraLastY = y;
            break;
        }
    }
}
