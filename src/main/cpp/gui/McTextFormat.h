#pragma once

#include "imgui.h"
#include <cstdint>
#include <string>

// Minecraft § 格式代码（原版语义）的解析、测量与绘制
//  - § 的两种字节形态都识别：Latin-1 单字节 0xA7，以及 UTF-8 的 0xC2 0xA7
//    （服务器 JSON 里的 § 通常是后者，只认前者会让整串颜色码变成字面字符）
//  - §0-§f 设色并清空格式位，§k/§l/§m/§n/§o 叠加，§r 全部还原
//  - §x 后跟 6 个 §十六进制（1.16+ 真彩色）与 &#RRGGBB 均支持
//  - §o 斜体只参与样式传递：没有斜体字形，绘制上不产生变化
//  - \n 是硬换行，样式跨 \n 延续（与原版组件串一致）
namespace McText {

struct Style {
    bool hasColor = false;
    uint8_t r = 255, g = 255, b = 255;
    bool bold = false;
    bool italic = false;
    bool underlined = false;
    bool strikethrough = false;
    bool obfuscated = false;
};

enum class Align { Left, Center };

// 颜色名（black ... white）或 #RRGGBB → style 的颜色字段；无法识别返回 false 且不改动 style
bool applyColorName(const std::string& name, Style& style);

// style → § 代码序列：命中 16 色发单个 §n，否则发 §x + 6 个十六进制码，之后补格式位。
// 输出使用 UTF-8 形态的 §，与本模块解析端一致
std::string styleToCodes(const Style& style);

// 尺寸（与 draw 相同的换行规则）。maxWidth <= 0 时不自动折行（\n 仍然换行）
ImVec2 measure(const std::string& text, float maxWidth = 0.0f);

// 绘制并返回右下角。maxWidth > 0 时按词换行（超长词按字硬切），\n 为硬换行；
// align 为 Center 时每行在 maxWidth 盒子内单独居中（原版断开连接界面的排版）
ImVec2 draw(ImDrawList* dl, float x, float y, const std::string& text,
            ImU32 defaultColor, float alpha = 1.0f,
            float maxWidth = 0.0f, Align align = Align::Left);
} // namespace McText
