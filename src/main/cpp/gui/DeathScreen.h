#pragma once

#include "Screen.h"
#include <string>
#include <functional>

// 死亡界面（对应 MC 的 DeathScreen）
class DeathScreen : public Screen {
public:
    using DisconnectCallback = std::function<void()>;

    void setDeathReason(const std::string& reason) { deathReason = reason; }
    void setDisconnectCallback(DisconnectCallback cb) { disconnectCallback = cb; }

    const char* getName() const override { return "DeathScreen"; }
    bool isPauseScreen() const override { return true; }
    void render(int mouseX, int mouseY) override;

private:
    std::string deathReason;
    DisconnectCallback disconnectCallback;
};
