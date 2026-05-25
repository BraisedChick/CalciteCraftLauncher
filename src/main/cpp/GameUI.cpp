#include "GameUI.h"
#include <android/log.h>

// ImGui 配置：OpenGL ES3 + 自定义加载器（使用 Android NDK 的 GLES3 头文件）
#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"

#define LOG_TAG "GameUI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

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
    // Android 系统通常有以下中文字体
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
        // 没有中文字体，使用默认字体（中文会显示为方块）
        font = io.Fonts->AddFontDefault();
        LOGE("No CJK font found, Chinese text may display as boxes");
    }

    // 设置字体缩放（适配移动端高 DPI）
    io.FontGlobalScale = 2.0f;

    // 设置样式
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

    // 初始化 OpenGL3 后端
    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("Failed to initialize ImGui OpenGL3 backend");
        return false;
    }

    initialized = true;
    LOGI("ImGui initialized successfully");
    return true;
}

void GameUI::shutdown() {
    if (!initialized) return;
    LOGI("Shutting down ImGui...");
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
            // down
            io.AddMouseButtonEvent(0, true);
        } else if (e.action == 1) {
            // up
            io.AddMouseButtonEvent(0, false);
        }
        // move: 坐标已经通过 AddMousePosEvent 更新
    }
}

void GameUI::addInputCharacter(unsigned int c) {
    if (!initialized) return;
    ImGuiIO& io = ImGui::GetIO();
    if (c == 127 || c == 8) {
        // 退格键：AddInputCharacter 无法处理控制字符，需要发送 key event
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
            renderMultiplayer();
            break;
        case UIState::CONNECTING:
            renderConnecting();
            break;
        case UIState::IN_GAME:
            break;
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

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

    // 标题
    ImGui::SetCursorPos(ImVec2(w * 0.5f - 120.0f, h * 0.12f));
    ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "MINECRAFT");

    float btnW = 280.0f;
    float btnH = 50.0f;
    float startY = h * 0.38f;
    float spacing = 12.0f;

    // 多人游戏
    ImGui::SetCursorPos(ImVec2(w * 0.5f - btnW * 0.5f, startY));
    if (ImGui::Button("多人游戏", ImVec2(btnW, btnH))) {
        currentState = UIState::MULTIPLAYER;
    }

    // 选项
    ImGui::SetCursorPos(ImVec2(w * 0.5f - btnW * 0.5f, startY + (btnH + spacing) * 1));
    if (ImGui::Button("选项", ImVec2(btnW, btnH))) {
        // 暂不实现
    }

    // 退出游戏
    ImGui::SetCursorPos(ImVec2(w * 0.5f - btnW * 0.5f, startY + (btnH + spacing) * 2));
    if (ImGui::Button("退出游戏", ImVec2(btnW, btnH))) {
        // 退出到桌面
    }

    // 底部小字：版本信息
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

    // 返回按钮
    ImGui::SetCursorPos(ImVec2(20, 20));
    if (ImGui::Button("< 返回", ImVec2(100, 40))) {
        currentState = UIState::MAIN_MENU;
    }

    float cx = w * 0.5f;
    float startY = h * 0.18f;

    // 服务器地址
    ImGui::SetCursorPos(ImVec2(cx - 170.0f, startY));
    ImGui::Text("服务器地址");
    ImGui::SetCursorPos(ImVec2(cx - 170.0f, startY + 32.0f));
    ImGui::PushItemWidth(340.0f);
    ImGui::InputText("##ip", ipBuffer, sizeof(ipBuffer));

    // 端口
    ImGui::SetCursorPos(ImVec2(cx - 170.0f, startY + 90.0f));
    ImGui::Text("端口");
    ImGui::SetCursorPos(ImVec2(cx - 170.0f, startY + 122.0f));
    ImGui::InputText("##port", portBuffer, sizeof(portBuffer));

    // 连接按钮
    ImGui::SetCursorPos(ImVec2(cx - 100.0f, startY + 180.0f));
    if (ImGui::Button("连接服务器", ImVec2(200, 50))) {
        int port = 25565;
        try {
            port = std::stoi(portBuffer);
        } catch (...) {
            port = 25565;
        }
        if (port <= 0) port = 25565;
        if (port > 65535) port = 25565;

        if (connectCallback) {
            currentState = UIState::CONNECTING;
            connectCallback(std::string(ipBuffer), port);
        }
    }

    ImGui::End();
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
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", ipBuffer);

    ImGui::End();
}
