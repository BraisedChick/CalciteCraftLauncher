#include "DeathScreen.h"

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "ClientEngine/ClientEngine.h"
#include "GameUI.h"
#include "GuiUtils.h"

void DeathScreen::render(int mouseX, int mouseY) {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    // 半透明红色遮罩（通过窗口背景色实现）
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(80, 0, 0, 160));

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("DeathScreen", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float BTN_W = 360.0f;
    const float BTN_H = 55.0f;
    const float SPACING = 20.0f;
    const float CENTER_X = w * 0.5f;

    // 你死了！
    float textW = ImGui::CalcTextSize("你死了！").x;
    ImGui::SetCursorPos(ImVec2(CENTER_X - textW * 0.5f, h * 0.3f));
    ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "你死了！");

    // 死亡原因
    textW = ImGui::CalcTextSize(deathReason.c_str()).x;
    ImGui::SetCursorPos(ImVec2(CENTER_X - textW * 0.5f, h * 0.3f + 45.0f));
    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", deathReason.c_str());

    // 重生按钮
    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, h * 0.5f));
    if (McButton("重生", ImVec2(BTN_W, BTN_H))) {
        GameUI::getInstance().setDeathScreenActive(false);
        auto* engine = ClientEngine::getInstance();
        if (engine) {
            engine->clearDeathMessage();
            engine->sendRespawn();
        }
    }

    // 标题屏幕按钮
    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, h * 0.5f + BTN_H + SPACING));
    if (McButton("标题屏幕", ImVec2(BTN_W, BTN_H))) {
        GameUI::getInstance().setDeathScreenActive(false);
        if (auto* engine = ClientEngine::getInstance()) {
            engine->clearDeathMessage();
        }
        if (disconnectCallback) {
            disconnectCallback();
        }
    }

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
}
