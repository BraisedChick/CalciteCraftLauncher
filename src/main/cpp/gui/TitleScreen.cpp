#include "TitleScreen.h"
#include "imgui.h"
#include "TextureLoader.h"
#include "ScreenManager.h"
#include "MultiplayerScreen.h"
#include "ConnectingScreen.h"
#include "PauseScreen.h"
#include "GameUI.h"
#include "GuiUtils.h"
#include "MinecraftVersion.h"
#include <android/log.h>

#define LOG_TAG "TitleScreen"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

void TitleScreen::render(int mouseX, int mouseY) {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("TitleScreen", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);

    // 标题图片（懒加载 minecraft.png）
    if (titleTextureID == 0) {
        TextureData tex = TextureLoader::loadPNG("gui/title/minecraft.png");
        if (tex.data && tex.width > 0 && tex.height > 0) {
            glGenTextures(1, &titleTextureID);
            glBindTexture(GL_TEXTURE_2D, titleTextureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            LOGI("Title texture loaded: %dx%d", tex.width, tex.height);
        }
    }

    // 副标题图片（懒加载 edition.png）
    if (editionTextureID == 0) {
        TextureData tex = TextureLoader::loadPNG("gui/title/edition.png");
        if (tex.data && tex.width > 0 && tex.height > 0) {
            glGenTextures(1, &editionTextureID);
            glBindTexture(GL_TEXTURE_2D, editionTextureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, tex.data);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            editionTexW = (float)tex.width;
            editionTexH = (float)tex.height;
            LOGI("Edition texture loaded: %dx%d", tex.width, tex.height);
        }
    }

    if (titleTextureID != 0) {
        float titleW = 600.0f;
        float titleH = titleW * 0.27f;
        ImGui::SetCursorPos(ImVec2(w * 0.5f - titleW * 0.5f, h * 0.10f));
        ImGui::Image((ImTextureID)(intptr_t)titleTextureID, ImVec2(titleW, titleH));

        if (editionTextureID != 0 && editionTexW > 0) {
            float editionDisplayW = editionTexW * 0.5f;
            float editionDisplayH = editionTexH * 0.5f;
            float editionY = h * 0.10f + titleH - editionDisplayH - 33.0f;
            ImGui::SetCursorPos(ImVec2(w * 0.5f - editionDisplayW * 0.5f, editionY));
            ImGui::Image((ImTextureID)(intptr_t)editionTextureID, ImVec2(editionDisplayW, editionDisplayH));
        }
    } else {
        ImGui::SetCursorPos(ImVec2(w * 0.5f - 120.0f, h * 0.12f));
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 0.2f, 1.0f), "MINECRAFT");
    }

    float btnW = 280.0f;
    float btnH = 50.0f;
    float startY = h * 0.38f;
    float spacing = 12.0f;

    // 多人游戏
    ImGui::SetCursorPos(ImVec2(w * 0.5f - btnW * 0.5f, startY));
    if (McButton("\xe5\xa4\x9a\xe4\xba\xba\xe6\xb8\xb8\xe6\x88\x8f", ImVec2(btnW, btnH))) {
        auto mp = std::make_unique<MultiplayerScreen>();
        auto connectCb = GameUI::getInstance().getConnectCallback();
        mp->setConnectCallback([connectCb](const std::string& ip, int port, const std::string& address) {
            GameUI::getInstance().setConnectingAddress(address);
            GameUI::getInstance().setState(UIState::CONNECTING);
            auto connecting = std::make_unique<ConnectingScreen>();
            connecting->setAddress(address);
            ScreenManager::getInstance().setScreen(std::move(connecting));
            if (connectCb) connectCb(ip, port);
        });
        mp->setBackCallback([]() {
            ScreenManager::getInstance().setScreen(std::make_unique<TitleScreen>());
        });
        ScreenManager::getInstance().setScreen(std::move(mp));
    }

    // 选项
    ImGui::SetCursorPos(ImVec2(w * 0.5f - btnW * 0.5f, startY + (btnH + spacing) * 1));
    if (McButton("\xe9\x80\x89\xe9\xa1\xb9", ImVec2(btnW, btnH))) {
        auto pause = std::make_unique<PauseScreen>();
        pause->setSubPage(PauseScreen::OPTIONS);
        pause->setFovCallback(GameUI::getInstance().fovCallback);
        pause->setRenderDistanceCallback(GameUI::getInstance().renderDistanceCallback);
        pause->setMipmapCallback(GameUI::getInstance().mipmapCallback);
        pause->setMaxFpsCallback(GameUI::getInstance().maxFpsCallback);
        pause->setSaveSettingsCallback([]() { GameUI::getInstance().saveSettingsNow(); });
        pause->setCloseCallback([]() {
            ScreenManager::getInstance().setScreen(std::make_unique<TitleScreen>());
        });
        ScreenManager::getInstance().setScreen(std::move(pause));
    }

    // 退出游戏
    ImGui::SetCursorPos(ImVec2(w * 0.5f - btnW * 0.5f, startY + (btnH + spacing) * 2));
    if (McButton("\xe9\x80\x80\xe5\x87\xba\xe6\xb8\xb8\xe6\x88\x8f", ImVec2(btnW, btnH))) {
        auto cb = GameUI::getInstance().getExitCallback();
        if (cb) cb();
    }

    ImGui::SetCursorPos(ImVec2(30.0f, h - 40.0f));
    std::string verLabel = "Minecraft " + std::string(getProtocolVersionName(PROTOCOL_VERSION));
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", verLabel.c_str());

    ImGui::End();
}
