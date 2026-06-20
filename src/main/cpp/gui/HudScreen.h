#pragma once

#include "Screen.h"
#include <GLES3/gl3.h>

// 游戏内 HUD（对应 MC 的 Gui / InGameHUD）
// 渲染：摇杆、按钮、准星、快捷栏、血条/饥饿/经验、F3 调试
class HudScreen : public Screen {
public:
    const char* getName() const override { return "HudScreen"; }
    bool isPauseScreen() const override { return false; }
    bool shouldRenderBackground() const override { return false; }
    void render(int mouseX, int mouseY) override;

    // GL 资源重置
    void resetGLResources() { hudTexturesLoaded = false; }

private:
    // HUD 纹理缓存
    GLuint texHeartContainer = 0;
    GLuint texHeartFull = 0;
    GLuint texHeartHalf = 0;
    GLuint texFoodEmpty = 0;
    GLuint texFoodFull = 0;
    GLuint texFoodHalf = 0;
    GLuint texExpBarBg = 0;
    GLuint texExpBarProgress = 0;
    bool hudTexturesLoaded = false;
};
