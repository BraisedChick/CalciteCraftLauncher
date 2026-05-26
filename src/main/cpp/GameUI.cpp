#include "GameUI.h"
#include <android/log.h>
#include <fstream>
#include <sstream>
#include <cstdio>

// ImGui 配置：OpenGL ES3 + 自定义加载器（使用 Android NDK 的 GLES3 头文件）
#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "CameraController.h"

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

// 服务器列表文件路径
#define SERVERS_FILE_PATH "/data/data/com.minecraft/files/servers.txt"

// 游戏内 UI 布局常量
#define JOYSTICK_CENTER_X  170.0f
#define JOYSTICK_CENTER_Y_OFFSET 160.0f  // 距屏幕底部
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

    // 底部按钮行：连接服务器 + 添加服务器 + 删除 + 返回
    float btnW = 130.0f;
    float btnGap = 12.0f;
    float totalW = btnW * 4 + btnGap * 3;
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
        showingAddServer = true;
    }

    // 删除服务器
    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(startX + (btnW + btnGap) * 2, btnY));
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
    ImGui::SetCursorPos(ImVec2(startX + (btnW + btnGap) * 3, btnY));
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
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "添加服务器");

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
        servers.push_back(server);
        selectedServer = (int)servers.size() - 1;
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
    if (action == 0) {
        // DOWN：分配触摸点，根据位置分配角色
        auto* pt = allocTouchPoint(pointerId);
        if (!pt) return;

        if (!isRoleTaken(TouchPoint::JOYSTICK) && isInJoystickArea(x, y)) {
            pt->role = TouchPoint::JOYSTICK;
            handleJoystickTouch(pointerId, x, y, action);
        } else if (!isRoleTaken(TouchPoint::UP_BUTTON) && isInUpButtonArea(x, y)) {
            pt->role = TouchPoint::UP_BUTTON;
            CameraController::getInstance().setKeyState(GAMEKEY_UP, true);
            buttons.upPressed = true;
        } else if (!isRoleTaken(TouchPoint::DOWN_BUTTON) && isInDownButtonArea(x, y)) {
            pt->role = TouchPoint::DOWN_BUTTON;
            CameraController::getInstance().setKeyState(GAMEKEY_DOWN, true);
            buttons.downPressed = true;
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
                CameraController::getInstance().setKeyState(GAMEKEY_UP, false);
                buttons.upPressed = false;
                break;
            case TouchPoint::DOWN_BUTTON:
                CameraController::getInstance().setKeyState(GAMEKEY_DOWN, false);
                buttons.downPressed = false;
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
            CameraController::getInstance().setJoystickInput(dx / JOYSTICK_MAX_DIST, dy / JOYSTICK_MAX_DIST);
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
                CameraController::getInstance().setJoystickInput(dx / JOYSTICK_MAX_DIST, dy / JOYSTICK_MAX_DIST);
            }
            break;
        }
        case 1: // UP
        case 3: // CANCEL
            joystick.active = false;
            joystick.knobX = 0;
            joystick.knobY = 0;
            CameraController::getInstance().setJoystickInput(0, 0);
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
