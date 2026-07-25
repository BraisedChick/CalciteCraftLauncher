#pragma once

#include "imgui.h"
#include "ResourcepackManager.h"
#include "MusicManager.h"
#include "ClientEngine/ClientEngine.h"
#include <string>
#include <cstdint>

// Minecraft § 颜色代码渲染工具
// 供多个 Screen 共享使用

inline ImU32 getMcColor(char code) {
    switch (code) {
        case '0': return IM_COL32(0, 0, 0, 255);
        case '1': return IM_COL32(0, 0, 170, 255);
        case '2': return IM_COL32(0, 170, 0, 255);
        case '3': return IM_COL32(0, 170, 170, 255);
        case '4': return IM_COL32(170, 0, 0, 255);
        case '5': return IM_COL32(170, 0, 170, 255);
        case '6': return IM_COL32(255, 170, 0, 255);
        case '7': return IM_COL32(170, 170, 170, 255);
        case '8': return IM_COL32(85, 85, 85, 255);
        case '9': return IM_COL32(85, 85, 255, 255);
        case 'a': return IM_COL32(85, 255, 85, 255);
        case 'b': return IM_COL32(85, 255, 255, 255);
        case 'c': return IM_COL32(255, 85, 85, 255);
        case 'd': return IM_COL32(255, 85, 255, 255);
        case 'e': return IM_COL32(255, 255, 85, 255);
        case 'f': return IM_COL32(255, 255, 255, 255);
        default:  return IM_COL32(255, 255, 255, 255);
    }
}

// 绘制带有 Minecraft § 颜色代码的文本（支持 UTF-8 多字节字符）
inline float drawMcText(float x, float y, const std::string& text, ImU32 defaultColor) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 currentColor = defaultColor;
    bool bold = false;
    float curX = x;
    size_t segStart = 0;

    for (size_t i = 0; i < text.size();) {
        if ((uint8_t)text[i] == 0xA7 && i + 1 < text.size()) {
            if (i > segStart) {
                const char* begin = text.c_str() + segStart;
                const char* endPtr = text.c_str() + i;
                drawList->AddText(ImVec2(curX + 1, y + 1), IM_COL32(0, 0, 0, 128), begin, endPtr);
                drawList->AddText(ImVec2(curX, y), currentColor, begin, endPtr);
                ImVec2 sz = ImGui::CalcTextSize(begin, endPtr);
                curX += sz.x;
                if (bold) {
                    drawList->AddText(ImVec2(curX - sz.x + 1, y), currentColor, begin, endPtr);
                }
            }
            char code = text[i + 1];
            if ((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') ||
                (code >= 'A' && code <= 'F')) {
                currentColor = getMcColor(code >= 'A' ? (code + 32) : code);
                bold = false;
            } else if (code == 'l' || code == 'L') {
                bold = true;
            } else if (code == 'o' || code == 'O') {
            } else if (code == 'r' || code == 'R') {
                currentColor = defaultColor;
                bold = false;
            }
            i += 2;
            segStart = i;
        } else {
            uint8_t c = (uint8_t)text[i];
            if (c < 0x80) i += 1;
            else if ((c & 0xE0) == 0xC0) i += 2;
            else if ((c & 0xF0) == 0xE0) i += 3;
            else if ((c & 0xF8) == 0xF0) i += 4;
            else i += 1;
        }
    }
    if (segStart < text.size()) {
        const char* begin = text.c_str() + segStart;
        const char* endPtr = text.c_str() + text.size();
        drawList->AddText(ImVec2(curX + 1, y + 1), IM_COL32(0, 0, 0, 128), begin, endPtr);
        drawList->AddText(ImVec2(curX, y), currentColor, begin, endPtr);
        ImVec2 sz = ImGui::CalcTextSize(begin, endPtr);
        curX += sz.x;
        if (bold) {
            drawList->AddText(ImVec2(curX - sz.x + 1, y), currentColor, begin, endPtr);
        }
    }
    return curX;
}

// ===== MC 风格九宫格按钮 =====

// 按钮纹理即时获取（不缓存 GL ID，由 ResourcepackManager 统一管理缓存）
inline void ensureWidgetTextures(GLuint& button, GLuint& buttonHighlighted, GLuint& buttonDisabled) {
    auto& rm = ResourcepackManager::getInstance();
    button = rm.getGuiTexture("sprites/widget/button");
    buttonHighlighted = rm.getGuiTexture("sprites/widget/button_highlighted");
    buttonDisabled = rm.getGuiTexture("sprites/widget/button_disabled");
}

// 九宫格绘制（MC .mcmeta nine_slice 规范）
// texW/texH = 纹理像素尺寸，border = 边框像素宽度
inline void drawNineSlice(ImDrawList* dl, GLuint tex,
    ImVec2 pos, ImVec2 size,
    float texW, float texH, float border)
{
    if (tex == 0) return;
    dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
        glBindSampler(0, 0);
    }, nullptr);

    float u[4] = { 0, border / texW, (texW - border) / texW, 1.0f };
    float v[4] = { 0, border / texH, (texH - border) / texH, 1.0f };
    float sx[4] = { pos.x, pos.x + border, pos.x + size.x - border, pos.x + size.x };
    float sy[4] = { pos.y, pos.y + border, pos.y + size.y - border, pos.y + size.y };

    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            dl->AddImage((ImTextureID)(intptr_t)tex,
                ImVec2(sx[c], sy[r]), ImVec2(sx[c + 1], sy[r + 1]),
                ImVec2(u[c], v[r]), ImVec2(u[c + 1], v[r + 1]));
        }
    }
}

// MC 风格按钮：九宫格纹理 + 居中文字 + 黑色阴影
// 返回 true 当按钮被点击（与 ImGui::Button 相同语义）
inline bool McButton(const char* label, ImVec2 size, bool enabled = true) {
    GLuint btn, btnHl, btnDis;
    ensureWidgetTextures(btn, btnHl, btnDis);

    // 不可见按钮做点击/悬停检测
    bool clicked = false;
    if (!enabled) ImGui::BeginDisabled();
    clicked = ImGui::InvisibleButton(label, size);
    if (!enabled) ImGui::EndDisabled();

    // 根据状态选择纹理
    GLuint tex;
    ImU32 textColor;
    if (!enabled) {
        tex = btnDis;
        textColor = IM_COL32(160, 160, 160, 255);
    } else if (ImGui::IsItemActive()) {
        tex = btnHl;
        textColor = IM_COL32(255, 255, 160, 255);
    } else if (ImGui::IsItemHovered()) {
        tex = btnHl;
        textColor = IM_COL32(255, 255, 160, 255);
    } else {
        tex = btn;
        textColor = IM_COL32(224, 224, 224, 255);
    }

    // 九宫格绘制纹理（200x20, 3px border）
    ImVec2 pos = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    drawNineSlice(dl, tex, pos, size, 200.0f, 20.0f, 3.0f);

    // 居中文字 + 黑色阴影
    ImVec2 textSize = ImGui::CalcTextSize(label);
    float tx = pos.x + (size.x - textSize.x) * 0.5f;
    float ty = pos.y + (size.y - textSize.y) * 0.5f;
    dl->AddText(ImVec2(tx + 1, ty + 1), IM_COL32(0, 0, 0, 128), label);
    dl->AddText(ImVec2(tx, ty), textColor, label);

    // 点击时播放音效
    if (clicked) {
        ClientEngine::getInstance()->getMusicManager()->playClickSound();
    }

    return clicked;
}
