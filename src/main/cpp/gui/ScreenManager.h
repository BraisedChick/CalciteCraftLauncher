#pragma once

#include "Screen.h"
#include <memory>

// Screen 管理器
// 管理当前活跃 Screen 的生命周期和切换
class ScreenManager {
public:
    static ScreenManager& getInstance();

    // 切换到新 Screen（类似 MC 的 minecraft.setScreen()）
    // 自动调用 oldScreen->removed() 和 newScreen->init()
    void setScreen(std::unique_ptr<Screen> newScreen);

    // 设置叠加层 Screen（如死亡界面、背包、暂停菜单）
    // overlay 渲染在当前 screen 之上
    void setOverlay(std::unique_ptr<Screen> overlay);

    // 每帧调用
    void tick();
    void render(int mouseX, int mouseY);

    // 获取当前 Screen
    Screen* getScreen() const { return currentScreen.get(); }
    Screen* getOverlay() const { return currentOverlay.get(); }

    // 是否有活跃 Screen
    bool hasScreen() const { return currentScreen != nullptr; }
    bool hasOverlay() const { return currentOverlay != nullptr; }

    // 关闭当前 overlay
    void closeOverlay() { setOverlay(nullptr); }

private:
    ScreenManager() = default;

    std::unique_ptr<Screen> currentScreen;
    std::unique_ptr<Screen> currentOverlay;
};
