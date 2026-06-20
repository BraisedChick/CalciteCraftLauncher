#pragma once

#include "Screen.h"
#include <GLES3/gl3.h>

// 主菜单界面（对应 MC 的 TitleScreen.java）
class TitleScreen : public Screen {
public:
    const char* getName() const override { return "TitleScreen"; }
    bool isPauseScreen() const override { return true; }

    void render(int mouseX, int mouseY) override;

private:

    GLuint titleTextureID = 0;
    GLuint editionTextureID = 0;
    float editionTexW = 0, editionTexH = 0;
};
