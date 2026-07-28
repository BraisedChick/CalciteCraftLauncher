#pragma once

#include "Screen.h"
#include "imgui.h"

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
    // HUD 纹理缓存（GL: GLuint / Vulkan: VkDescriptorSet，统一存 ImTextureID）
    ImTextureID texHeartContainer = 0;
    ImTextureID texHeartFull = 0;
    ImTextureID texHeartHalf = 0;
    ImTextureID texFoodEmpty = 0;
    ImTextureID texFoodFull = 0;
    ImTextureID texFoodHalf = 0;
    ImTextureID texExpBarBg = 0;
    ImTextureID texExpBarProgress = 0;
    bool hudTexturesLoaded = false;
};
