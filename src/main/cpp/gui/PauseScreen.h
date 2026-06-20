#pragma once

#include "Screen.h"
#include <GLES3/gl3.h>
#include <functional>

// 暂停菜单（对应 MC 的 PauseScreen）
// 包含：暂停菜单 + 选项 + 视频设置
class PauseScreen : public Screen {
public:
    using DisconnectCallback = std::function<void()>;
    using FovCallback = std::function<void(float)>;
    using RenderDistanceCallback = std::function<void(int)>;
    using MipmapCallback = std::function<void(bool)>;
    using MaxFpsCallback = std::function<void(int)>;
    using SaveSettingsCallback = std::function<void()>;
    using CloseCallback = std::function<void()>;

    void setDisconnectCallback(DisconnectCallback cb) { disconnectCallback = cb; }
    void setFovCallback(FovCallback cb) { fovCallback = cb; }
    void setRenderDistanceCallback(RenderDistanceCallback cb) { renderDistanceCallback = cb; }
    void setMipmapCallback(MipmapCallback cb) { mipmapCallback = cb; }
    void setMaxFpsCallback(MaxFpsCallback cb) { maxFpsCallback = cb; }
    void setSaveSettingsCallback(SaveSettingsCallback cb) { saveSettingsCallback = cb; }
    void setCloseCallback(CloseCallback cb) { closeCallback = cb; }

    const char* getName() const override { return "PauseScreen"; }
    bool isPauseScreen() const override { return true; }
    void render(int mouseX, int mouseY) override;

    // 当前子页面
    enum SubPage { MENU, OPTIONS, VIDEO_SETTINGS };
    void setSubPage(SubPage page) { subPage = page; }

private:
    void renderPauseMenu();
    void renderOptions();
    void renderVideoSettings();

    SubPage subPage = MENU;
    DisconnectCallback disconnectCallback;
    FovCallback fovCallback;
    RenderDistanceCallback renderDistanceCallback;
    MipmapCallback mipmapCallback;
    MaxFpsCallback maxFpsCallback;
    SaveSettingsCallback saveSettingsCallback;
    CloseCallback closeCallback;
};
