#pragma once

#include "Screen.h"
#include <string>
#include <functional>

// 断开连接界面（对应 MC 的 DisconnectedScreen）
// 显示连接失败/被踢原因，点击"返回标题界面"回到主菜单
class DisconnectedScreen : public Screen {
public:
    using BackCallback = std::function<void()>;

    void setTitle(const std::string& t) { title = t; }
    void setReason(const std::string& r) { reason = r; }
    void setBackCallback(BackCallback cb) { backCallback = cb; }

    const char* getName() const override { return "DisconnectedScreen"; }
    bool isPauseScreen() const override { return true; }
    void render(int mouseX, int mouseY) override;

private:
    std::string title = "连接失败";
    std::string reason;
    BackCallback backCallback;
};
