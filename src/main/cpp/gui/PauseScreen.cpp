#include "PauseScreen.h"

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "GameUI.h"
#include "GuiUtils.h"

void PauseScreen::render(int mouseX, int mouseY) {
    switch (subPage) {
        case VIDEO_SETTINGS: renderVideoSettings(); break;
        case OPTIONS: renderOptions(); break;
        default: renderPauseMenu(); break;
    }
}

void PauseScreen::renderPauseMenu() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 180));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("GameMenu", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float BTN_W = 220.0f;
    const float BTN_H = 50.0f;
    const float SPACING = 20.0f;
    const float CENTER_X = w * 0.5f;
    float startY = h * 0.5f - BTN_H * 1.5f - SPACING;

    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, startY));
    if (McButton("回到游戏", ImVec2(BTN_W, BTN_H))) {
        GameUI::getInstance().setGameMenuOpen(false);
        if (closeCallback) closeCallback();
    }

    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, startY + BTN_H + SPACING));
    if (McButton("选项", ImVec2(BTN_W, BTN_H))) {
        subPage = OPTIONS;
    }

    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, startY + (BTN_H + SPACING) * 2));
    if (McButton("断开连接", ImVec2(BTN_W, BTN_H))) {
        GameUI::getInstance().setGameMenuOpen(false);
        if (disconnectCallback) disconnectCallback();
    }

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
}

void PauseScreen::renderOptions() {
    if (subPage == VIDEO_SETTINGS) {
        renderVideoSettings();
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 180));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("GameOptions", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    auto& gui = GameUI::getInstance();
    const float PANEL_W = 300.0f;
    const float PANEL_H = 260.0f;
    float panelX = w * 0.5f - PANEL_W * 0.5f;
    float panelY = h * 0.5f - PANEL_H * 0.5f;

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(panelX, panelY), ImVec2(panelX + PANEL_W, panelY + PANEL_H),
        IM_COL32(40, 40, 50, 220), 8.0f);
    ImGui::GetWindowDrawList()->AddRect(
        ImVec2(panelX, panelY), ImVec2(panelX + PANEL_W, panelY + PANEL_H),
        IM_COL32(100, 100, 120, 255), 8.0f);

    // FOV 滑块
    float fov = gui.getOptionsFov();
    ImGui::SetCursorPos(ImVec2(panelX + 20, panelY + 30));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(60, 60, 70, 200));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(120, 180, 255, 220));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(150, 200, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::SetNextItemWidth(PANEL_W - 40);
    if (ImGui::SliderFloat("##fov", &fov, 30.0f, 120.0f, "视场角: %.0f°")) {
        // Update via GameUI (it will forward to the callback)
        gui.setOptionsFov(fov);
        if (fovCallback) fovCallback(fov);
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    // 视频设置按钮
    ImGui::SetCursorPos(ImVec2(panelX + PANEL_W * 0.5f - 100.0f, panelY + 95));
    if (McButton("视频设置", ImVec2(200, 40))) {
        subPage = VIDEO_SETTINGS;
    }

    // 完成按钮
    ImGui::SetCursorPos(ImVec2(panelX + PANEL_W * 0.5f - 60.0f, panelY + PANEL_H - 50));
    if (McButton("完成", ImVec2(120, 40))) {
        gui.setOptionsOpen(false);
        gui.setVideoSettingsOpen(false);
        if (saveSettingsCallback) saveSettingsCallback();
        if (closeCallback) {
            gui.setGameMenuOpen(false);
            closeCallback();
        } else {
            subPage = MENU;
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
}

void PauseScreen::renderVideoSettings() {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 180));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("VideoSettings", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    auto& gui = GameUI::getInstance();
    const float PANEL_W = 300.0f;
    const float PANEL_H = 360.0f;
    float panelX = w * 0.5f - PANEL_W * 0.5f;
    float panelY = h * 0.5f - PANEL_H * 0.5f;

    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(panelX, panelY), ImVec2(panelX + PANEL_W, panelY + PANEL_H),
        IM_COL32(40, 40, 50, 220), 8.0f);
    ImGui::GetWindowDrawList()->AddRect(
        ImVec2(panelX, panelY), ImVec2(panelX + PANEL_W, panelY + PANEL_H),
        IM_COL32(100, 100, 120, 255), 8.0f);

    // 渲染距离
    int rd = gui.getRenderDistance();
    ImGui::SetCursorPos(ImVec2(panelX + 20, panelY + 40));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(60, 60, 70, 200));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(120, 180, 255, 220));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(150, 200, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    ImGui::SetNextItemWidth(PANEL_W - 40);
    if (ImGui::SliderInt("##renderDist", &rd, 2, 20, "渲染距离: %d 区块")) {
        gui.setRenderDistanceDirect(rd);
        if (renderDistanceCallback) renderDistanceCallback(rd);
    }
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    // 平滑光照
    bool smooth = gui.isSmoothLightingEnabled();
    ImGui::SetCursorPos(ImVec2(panelX + 20, panelY + 90));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    if (ImGui::Checkbox("平滑光照", &smooth)) {
        gui.setSmoothLightingDirect(smooth);
    }
    ImGui::PopStyleColor();

    // Mipmap
    bool mipmap = gui.isMipmapEnabled();
    ImGui::SetCursorPos(ImVec2(panelX + 20, panelY + 130));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
    if (ImGui::Checkbox("Mipmap", &mipmap)) {
        gui.setMipmapDirect(mipmap);
        if (mipmapCallback) mipmapCallback(mipmap);
    }
    ImGui::PopStyleColor();

    // 最大帧率
    int fps = gui.getMaxFps();
    ImGui::SetCursorPos(ImVec2(panelX + 20, panelY + 170));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(60, 60, 70, 200));
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, IM_COL32(120, 180, 255, 220));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(150, 200, 255, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));

    char fpsLabel[64];
    if (fps == 0) snprintf(fpsLabel, sizeof(fpsLabel), "最大帧率: 垂直同步");
    else if (fps >= 256) snprintf(fpsLabel, sizeof(fpsLabel), "最大帧率: 无限制");
    else snprintf(fpsLabel, sizeof(fpsLabel), "最大帧率: %d fps", fps);

    ImGui::SetNextItemWidth(PANEL_W - 40);
    if (ImGui::SliderInt("##maxFps", &fps, 0, 256, "")) {
        gui.setMaxFpsDirect(fps);
        if (maxFpsCallback) maxFpsCallback(fps);
    }
    ImGui::SameLine();
    ImGui::SetCursorPosX(panelX + 20);
    ImGui::Text("%s", fpsLabel);
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();

    // 完成按钮
    ImGui::SetCursorPos(ImVec2(panelX + PANEL_W * 0.5f - 60.0f, panelY + PANEL_H - 55));
    if (McButton("完成", ImVec2(120, 40))) {
        gui.setVideoSettingsOpen(false);
        if (saveSettingsCallback) saveSettingsCallback();
        subPage = OPTIONS;
    }

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
}
