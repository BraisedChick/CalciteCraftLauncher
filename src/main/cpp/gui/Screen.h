#pragma once

#include <string>
#include <vector>
#include <functional>

// Minecraft 风格 Screen 基类
// 生命周期: init() → tick()/render() → removed()
// 类似 MC 的 Screen.java，每个 Screen 是一个完整的 UI 页面
class Screen {
public:
    virtual ~Screen() = default;

    // 初始化：创建组件（类似 MC 的 Screen.init()）
    // 在 setScreen 时调用，width/height 为屏幕尺寸
    virtual void init(int width, int height) {}

    // 逻辑更新（每帧调用，类似 MC 的 Screen.tick()）
    virtual void tick() {}

    // 渲染（每帧调用，类似 MC 的 Screen.render()）
    virtual void render(int mouseX, int mouseY) = 0;

    // 清理（被切换走时调用，类似 MC 的 Screen.removed()）
    virtual void removed() {}

    // 是否暂停游戏（类似 MC 的 Screen.isPauseScreen()）
    virtual bool isPauseScreen() const { return true; }

    // 是否渲染背景
    virtual bool shouldRenderBackground() const { return true; }

    // 返回 Screen 名称（调试用）
    virtual const char* getName() const { return "Screen"; }
};
