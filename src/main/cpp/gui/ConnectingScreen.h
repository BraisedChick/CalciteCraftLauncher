#pragma once

#include "Screen.h"
#include <string>

// 连接中界面（对应 MC 的 ConnectScreen）
class ConnectingScreen : public Screen {
public:
    void setAddress(const std::string& addr) { connectingAddress = addr; }

    const char* getName() const override { return "ConnectingScreen"; }
    bool isPauseScreen() const override { return true; }
    void render(int mouseX, int mouseY) override;

private:
    std::string connectingAddress;
};
