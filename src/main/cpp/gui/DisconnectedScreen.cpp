#include "DisconnectedScreen.h"

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "GuiUtils.h"

void DisconnectedScreen::render(int mouseX, int mouseY) {
    ImGuiIO& io = ImGui::GetIO();
    float w = io.DisplaySize.x;
    float h = io.DisplaySize.y;

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Disconnected", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    const float CENTER_X = w * 0.5f;

    // 标题（连接已丢失 / 无法连接至服务器，由 ClientEngine 按断开阶段设置）
    float textW = ImGui::CalcTextSize(title.c_str()).x;
    ImGui::SetCursorPos(ImVec2(CENTER_X - textW * 0.5f, h * 0.25f));
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", title.c_str());

    // 原因文本（可能多行/带 § 样式，限宽自动换行并居中排版）
    const float WRAP_W = w * 0.6f;
    ImGui::SetCursorPos(ImVec2(CENTER_X - WRAP_W * 0.5f, h * 0.25f + 50.0f));
    ImVec2 reasonPos = ImGui::GetCursorScreenPos();
    McText::draw(ImGui::GetWindowDrawList(), reasonPos.x, reasonPos.y, reason,
                 IM_COL32(204, 204, 204, 255), 1.0f, WRAP_W, McText::Align::Center);

    // 返回按钮（对应原版 "回到服务器列表"）
    const float BTN_W = 360.0f;
    const float BTN_H = 55.0f;
    ImGui::SetCursorPos(ImVec2(CENTER_X - BTN_W * 0.5f, h * 0.62f));
    if (McButton("回到服务器列表", ImVec2(BTN_W, BTN_H))) {
        if (backCallback) {
            backCallback();
        }
    }

    ImGui::End();
}
