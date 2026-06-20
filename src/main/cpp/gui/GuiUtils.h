#pragma once

#include "imgui.h"
#include <string>

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
