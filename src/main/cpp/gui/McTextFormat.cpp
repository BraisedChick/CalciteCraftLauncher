#include "McTextFormat.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace McText {
namespace {

// §0-§f 及其颜色名（原版注册名）
struct PaletteEntry {
    char code;
    const char* name;
    uint8_t r, g, b;
};

const PaletteEntry PALETTE[16] = {
    {'0', "black",        0,   0,   0},
    {'1', "dark_blue",    0,   0,   170},
    {'2', "dark_green",   0,   170, 0},
    {'3', "dark_aqua",    0,   170, 170},
    {'4', "dark_red",     170, 0,   0},
    {'5', "dark_purple",  170, 0,   170},
    {'6', "gold",         255, 170, 0},
    {'7', "gray",         170, 170, 170},
    {'8', "dark_gray",    85,  85,  85},
    {'9', "blue",         85,  85,  255},
    {'a', "green",        85,  255, 85},
    {'b', "aqua",         85,  255, 255},
    {'c', "red",          255, 85,  85},
    {'d', "light_purple", 255, 85,  255},
    {'e', "yellow",       255, 255, 85},
    {'f', "white",        255, 255, 255},
};

bool hexDigitValue(char c, int& value) {
    if (c >= '0' && c <= '9') { value = c - '0'; return true; }
    if (c >= 'a' && c <= 'f') { value = c - 'a' + 10; return true; }
    if (c >= 'A' && c <= 'F') { value = c - 'A' + 10; return true; }
    return false;
}

// 单个 UTF-8 字符的字节数；游离的续字节（例如裸写的 0xA7）按 1 处理
size_t utf8CharLen(uint8_t c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

// 识别 § 代码标记，两种字节形态都接受
bool readSectionMark(const std::string& t, size_t pos, char& code, size_t& len) {
    if (pos >= t.size()) return false;      // 串尾以 § 结束时不留越界读
    if ((uint8_t)t[pos] == 0xC2) {
        if (pos + 2 < t.size() && (uint8_t)t[pos + 1] == 0xA7) {
            code = t[pos + 2];
            len = 3;
            return true;
        }
        return false;
    }
    if ((uint8_t)t[pos] == 0xA7 && pos + 1 < t.size()) {
        code = t[pos + 1];
        len = 2;
        return true;
    }
    return false;
}

void applyPaletteColor(Style& st, const PaletteEntry& e) {
    st = Style();          // 原版语义：颜色码同时清掉所有格式位
    st.hasColor = true;
    st.r = e.r;
    st.g = e.g;
    st.b = e.b;
}

void applyRgb(Style& st, int r1, int r2, int g1, int g2, int b1, int b2) {
    st = Style();
    st.hasColor = true;
    st.r = (uint8_t)((r1 << 4) | r2);
    st.g = (uint8_t)((g1 << 4) | g2);
    st.b = (uint8_t)((b1 << 4) | b2);
}

// pos 处若是已知格式代码：更新 st 并返回消耗字节数；否则返回 0（当普通文本处理）
size_t applyCodeAt(const std::string& t, size_t pos, Style& st) {
    char code = 0;
    size_t mark = 0;
    if (!readSectionMark(t, pos, code, mark)) {
        // &#RRGGBB（部分服务器 MOTD 的写法）
        if (t[pos] == '&' && pos + 7 < t.size() && t[pos + 1] == '#') {
            int d[6];
            size_t k = 0;
            while (k < 6 && hexDigitValue(t[pos + 2 + k], d[k])) ++k;
            if (k == 6) {
                applyRgb(st, d[0], d[1], d[2], d[3], d[4], d[5]);
                return 8;
            }
        }
        return 0;
    }

    char c = (char)std::tolower((unsigned char)code);
    if (c >= '0' && c <= '9') { applyPaletteColor(st, PALETTE[c - '0']); return mark; }
    if (c >= 'a' && c <= 'f') { applyPaletteColor(st, PALETTE[10 + (c - 'a')]); return mark; }

    switch (c) {
        case 'k': st.obfuscated = true; return mark;
        case 'l': st.bold = true; return mark;
        case 'm': st.strikethrough = true; return mark;
        case 'n': st.underlined = true; return mark;
        case 'o': st.italic = true; return mark;
        case 'r': st = Style(); return mark;
        case 'x': {
            // §x 后应跟 6 个 §十六进制码（1.16+ 真彩色），不完整就只吃掉 §x 本身
            size_t p = pos + mark;
            int d[6];
            int k = 0;
            while (k < 6) {
                char cc = 0;
                size_t m = 0;
                if (!readSectionMark(t, p, cc, m)) break;
                int v = 0;
                if (!hexDigitValue(cc, v)) break;
                d[k++] = v;
                p += m;
            }
            if (k == 6) {
                applyRgb(st, d[0], d[1], d[2], d[3], d[4], d[5]);
                return p - pos;
            }
            return mark;
        }
        default: return 0;
    }
}

struct Piece {
    size_t begin = 0;
    size_t end = 0;
    Style style;
    float width = 0.0f;
    bool breakBefore = false;   // 词首（串首或空格之后），允许在此断行
    bool space = false;         // 纯空格片段
    bool newline = false;       // 硬换行占位（不产生字形）
};

float pieceWidth(const std::string& t, size_t begin, size_t end) {
    return ImGui::CalcTextSize(t.data() + begin, t.data() + end).x;
}

// 切成"同一样式 + 同一词"的片段；空格与换行单独成片（行首丢弃、行尾裁剪都靠它）
std::vector<Piece> buildPieces(const std::string& t) {
    std::vector<Piece> pieces;
    Style st;
    bool nextBreak = true;
    size_t runStart = 0;
    size_t i = 0;

    auto flush = [&](size_t upto) {
        if (upto <= runStart) return;   // 空片段（例如词首的 § 码）不影响可断行标记
        Piece p;
        p.begin = runStart;
        p.end = upto;
        p.style = st;
        p.breakBefore = nextBreak;
        p.width = pieceWidth(t, p.begin, p.end);
        pieces.push_back(p);
        runStart = upto;
        nextBreak = false;
    };

    while (i < t.size()) {
        size_t used = applyCodeAt(t, i, st);
        if (used) {
            flush(i);
            i += used;
            runStart = i;
            continue;
        }

        size_t len = utf8CharLen((uint8_t)t[i]);
        if (t[i] == '\n') {
            flush(i);
            Piece nl;
            nl.begin = i;
            nl.end = i + len;
            nl.style = st;
            nl.newline = true;
            pieces.push_back(nl);
            i += len;
            runStart = i;
            nextBreak = true;   // 换行之后的词可以换行
            continue;
        }
        if (t[i] == ' ') {
            flush(i);
            Piece sp;
            sp.begin = i;
            sp.end = i + len;
            sp.style = st;
            sp.space = true;
            sp.width = pieceWidth(t, sp.begin, sp.end);
            pieces.push_back(sp);
            i += len;
            runStart = i;
            nextBreak = true;   // 空格之后的词可以换行
            continue;
        }

        i += len;
    }
    flush(t.size());
    return pieces;
}

// 超过整行宽度的词（无空格可断）按字硬切
std::vector<Piece> splitLongPieces(const std::string& t, const std::vector<Piece>& in, float maxWidth) {
    std::vector<Piece> out;
    for (const Piece& p : in) {
        if (p.space || p.width <= maxWidth) {
            out.push_back(p);
            continue;
        }
        size_t chunkStart = p.begin;
        size_t i = p.begin;
        float acc = 0.0f;
        bool firstChunk = true;
        while (i < p.end) {
            size_t len = utf8CharLen((uint8_t)t[i]);
            float cw = pieceWidth(t, i, i + len);
            if (acc > 0.0f && acc + cw > maxWidth) {
                Piece chunk;
                chunk.begin = chunkStart;
                chunk.end = i;
                chunk.style = p.style;
                chunk.width = acc;
                chunk.breakBefore = firstChunk ? p.breakBefore : false;
                out.push_back(chunk);
                firstChunk = false;
                chunkStart = i;
                acc = 0.0f;
            }
            acc += cw;
            i += len;
        }
        if (chunkStart < p.end) {
            Piece chunk;
            chunk.begin = chunkStart;
            chunk.end = p.end;
            chunk.style = p.style;
            chunk.width = acc;
            chunk.breakBefore = firstChunk ? p.breakBefore : false;
            out.push_back(chunk);
        }
    }
    return out;
}

struct Line {
    size_t first = 0;
    size_t last = 0;   // [first, last)
    float width = 0.0f;
};

void layoutLines(const std::vector<Piece>& pieces, float maxWidth, std::vector<Line>& lines) {
    lines.clear();
    if (pieces.empty()) return;

    // force：由 \n 结束的行，即便没有可见片段也要保留空行占位
    auto closeLine = [&](size_t first, size_t last, float w, bool force = false) {
        while (last > first && pieces[last - 1].space) {   // 行尾空格不占宽
            --last;
            w -= pieces[last].width;
        }
        Line ln;
        ln.first = first;
        ln.width = 0.0f;
        if (last <= first) {
            if (!force) return;
            ln.last = first;
            lines.push_back(ln);
            return;
        }
        ln.last = last;
        ln.width = w;
        lines.push_back(ln);
    };

    size_t start = 0;
    float w = 0.0f;
    for (size_t k = 0; k < pieces.size(); ++k) {
        const Piece& p = pieces[k];
        if (p.newline) {                 // 硬换行不受 maxWidth 影响
            closeLine(start, k, w, true);
            start = k + 1;
            w = 0.0f;
            continue;
        }
        if (maxWidth <= 0.0f) {          // 单行模式：不自动折行
            w += p.width;
            continue;
        }
        if (w == 0.0f && p.space) {      // 行首空格丢弃
            start = k + 1;
            continue;
        }
        if (p.breakBefore && w > 0.0f && w + p.width > maxWidth) {
            closeLine(start, k, w);
            start = k;
            w = 0.0f;
            if (p.space) {   // 断行处的空格也不留下
                start = k + 1;
                continue;
            }
        }
        w += p.width;
    }
    closeLine(start, pieces.size(), w);
}

// §k 混淆：每个字形换成随机字形，约每秒变化 8 次（原版节奏）
uint32_t mixHash(uint32_t a) {
    a ^= a >> 16;
    a *= 0x7FEB352Du;
    a ^= a >> 15;
    a *= 0x846CA68Bu;
    a ^= a >> 16;
    return a;
}

std::string scrambleText(const std::string& t, const Piece& p, int frame) {
    static const char SYMBOLS[] =
        "abcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*()-_=+[]{}|;:,.<>?/~";
    const size_t symbolCount = sizeof(SYMBOLS) - 1;
    std::string out;
    size_t i = p.begin;
    while (i < p.end) {
        size_t len = utf8CharLen((uint8_t)t[i]);
        if (t[i] == ' ') {
            out.push_back(' ');
        } else {
            uint32_t h = mixHash((uint32_t)(i * 2654435761u) ^ (uint32_t)(frame * 0x1B873593u));
            out.push_back(SYMBOLS[h % symbolCount]);
        }
        i += len;
    }
    return out;
}

ImU32 scaledAlpha(ImU32 color, float alpha) {
    if (alpha >= 1.0f) return color;
    return (color & 0x00FFFFFFu) | ((ImU32)(alpha * 255.0f) << 24);
}

} // namespace

bool applyColorName(const std::string& name, Style& style) {
    for (const PaletteEntry& e : PALETTE) {
        if (name == e.name) {
            style.hasColor = true;
            style.r = e.r;
            style.g = e.g;
            style.b = e.b;
            return true;
        }
    }
    if (name.size() == 7 && name[0] == '#') {
        int d[6];
        size_t k = 0;
        while (k < 6 && hexDigitValue(name[2 + k], d[k])) ++k;
        if (k == 6) {
            style.hasColor = true;
            style.r = (uint8_t)((d[0] << 4) | d[1]);
            style.g = (uint8_t)((d[2] << 4) | d[3]);
            style.b = (uint8_t)((d[4] << 4) | d[5]);
            return true;
        }
    }
    return false;
}

std::string styleToCodes(const Style& style) {
    static const char SECTION[] = "\xC2\xA7";   // UTF-8 形态的 §
    std::string out;

    if (style.hasColor) {
        const PaletteEntry* exact = nullptr;
        for (const PaletteEntry& e : PALETTE) {
            if (e.r == style.r && e.g == style.g && e.b == style.b) {
                exact = &e;
                break;
            }
        }
        if (exact) {
            out += SECTION;
            out += exact->code;
        } else {
            static const char HEXDIGITS[] = "0123456789abcdef";
            const uint8_t bytes[3] = {style.r, style.g, style.b};
            out += SECTION;
            out += 'x';
            for (uint8_t b : bytes) {
                out += SECTION; out += HEXDIGITS[b >> 4];
                out += SECTION; out += HEXDIGITS[b & 0x0F];
            }
        }
    }

    // 格式位排在颜色之后：颜色码会清空已生效的格式位
    if (style.obfuscated)    { out += SECTION; out += 'k'; }
    if (style.bold)          { out += SECTION; out += 'l'; }
    if (style.strikethrough) { out += SECTION; out += 'm'; }
    if (style.underlined)    { out += SECTION; out += 'n'; }
    if (style.italic)        { out += SECTION; out += 'o'; }
    return out;
}

ImVec2 measure(const std::string& text, float maxWidth) {
    std::vector<Piece> pieces = buildPieces(text);
    if (maxWidth > 0.0f) pieces = splitLongPieces(text, pieces, maxWidth);
    std::vector<Line> lines;
    layoutLines(pieces, maxWidth, lines);

    float w = 0.0f;
    for (const Line& ln : lines) w = std::max(w, ln.width);
    float h = (float)std::max<size_t>(lines.size(), 1) * ImGui::GetTextLineHeight();
    return ImVec2(w, h);
}

ImVec2 draw(ImDrawList* dl, float x, float y, const std::string& text,
            ImU32 defaultColor, float alpha, float maxWidth, Align align) {
    std::vector<Piece> pieces = buildPieces(text);
    if (maxWidth > 0.0f) pieces = splitLongPieces(text, pieces, maxWidth);
    std::vector<Line> lines;
    layoutLines(pieces, maxWidth, lines);

    const float lineHeight = ImGui::GetTextLineHeight();
    // 下划线定位用基线（ImGui 1.92 把 Ascent 移到 ImFontBaked，且已缩放到像素尺寸）
    const ImFontBaked* baked = ImGui::GetFontBaked();
    const float baseline = baked ? baked->Ascent : lineHeight * 0.8f;
    const int scrambleFrame = (int)(ImGui::GetTime() * 8.0);
    const ImU32 shadowColor = IM_COL32(0, 0, 0, (int)(128.0f * alpha));

    float bottom = y;
    float right = x;
    if (lines.empty()) return ImVec2(x, y + lineHeight);

    for (const Line& ln : lines) {
        float lx = x;
        if (align == Align::Center && maxWidth > 0.0f) lx += (maxWidth - ln.width) * 0.5f;

        for (size_t k = ln.first; k < ln.last; ++k) {
            const Piece& p = pieces[k];
            std::string scrambled;
            const char* begin = text.data() + p.begin;
            const char* end = text.data() + p.end;
            if (p.style.obfuscated) {
                scrambled = scrambleText(text, p, scrambleFrame);
                begin = scrambled.data();
                end = begin + scrambled.size();
            }

            ImU32 color = p.style.hasColor
                ? IM_COL32(p.style.r, p.style.g, p.style.b, 255)
                : defaultColor;
            color = scaledAlpha(color, alpha);

            dl->AddText(ImVec2(lx + 1.0f, bottom + 1.0f), shadowColor, begin, end);
            dl->AddText(ImVec2(lx, bottom), color, begin, end);
            if (p.style.bold) {
                dl->AddText(ImVec2(lx + 1.0f, bottom), color, begin, end);
            }
            if (p.style.underlined) {
                float uy = bottom + baseline + 1.0f;
                dl->AddLine(ImVec2(lx, uy), ImVec2(lx + p.width, uy), color);
            }
            if (p.style.strikethrough) {
                float sy = bottom + lineHeight * 0.5f;
                dl->AddLine(ImVec2(lx, sy), ImVec2(lx + p.width, sy), color);
            }
            lx += p.width;
        }

        right = std::max(right, lx);
        bottom += lineHeight;
    }
    return ImVec2(right, bottom);
}

} // namespace McText
