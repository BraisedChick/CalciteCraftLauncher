#pragma once

#include "Screen.h"
#include <GLES3/gl3.h>

// 主菜单界面（对应 MC 的 TitleScreen.java）
class TitleScreen : public Screen {
public:
    const char* getName() const override { return "TitleScreen"; }
    bool isPauseScreen() const override { return true; }

    void render(int mouseX, int mouseY) override;

    // 打开多人游戏服务器列表（装配连接/返回回调后切屏），
    // 供标题界面按钮与 DisconnectedScreen 返回按钮复用
    static void openMultiplayerScreen();

private:

    GLuint titleTextureID = 0;
    GLuint editionTextureID = 0;
    float editionTexW = 0, editionTexH = 0;
};
