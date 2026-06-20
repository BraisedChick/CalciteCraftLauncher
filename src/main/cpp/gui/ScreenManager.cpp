#include "ScreenManager.h"
#include "imgui.h"
#include <android/log.h>

#define LOG_TAG "ScreenManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

ScreenManager& ScreenManager::getInstance() {
    static ScreenManager instance;
    return instance;
}

void ScreenManager::setScreen(std::unique_ptr<Screen> newScreen) {
    // 旧 Screen 清理
    if (currentScreen) {
        LOGI("Removing screen: %s", currentScreen->getName());
        currentScreen->removed();
    }

    // 切换
    currentScreen = std::move(newScreen);

    // 新 Screen 初始化
    if (currentScreen) {
        ImGuiIO& io = ImGui::GetIO();
        currentScreen->init((int)io.DisplaySize.x, (int)io.DisplaySize.y);
        LOGI("Set screen: %s", currentScreen->getName());
    }

    // 切换 Screen 时关闭 overlay
    currentOverlay.reset();
}

void ScreenManager::setOverlay(std::unique_ptr<Screen> overlay) {
    if (currentOverlay) {
        LOGI("Removing overlay: %s", currentOverlay->getName());
        currentOverlay->removed();
    }

    currentOverlay = std::move(overlay);

    if (currentOverlay) {
        ImGuiIO& io = ImGui::GetIO();
        currentOverlay->init((int)io.DisplaySize.x, (int)io.DisplaySize.y);
        LOGI("Set overlay: %s", currentOverlay->getName());
    }
}

void ScreenManager::tick() {
    if (currentScreen) {
        currentScreen->tick();
    }
    if (currentOverlay) {
        currentOverlay->tick();
    }
}

void ScreenManager::render(int mouseX, int mouseY) {
    // 渲染主 Screen
    if (currentScreen) {
        currentScreen->render(mouseX, mouseY);
    }

    // 渲染叠加层（在 Screen 之上）
    if (currentOverlay) {
        currentOverlay->render(mouseX, mouseY);
    }
}
