#include "ChatComponentText.h"

#include <cstdlib>
#include <vector>

#include "gui/McTextFormat.h"

namespace ChatComponent {
namespace {

using nlohmann::json;

// 布尔字段可能是 true/false 或字符串 "true"/"false"（原版两种写法都会出现）
bool readFlag(const json& o, const char* key, bool& out) {
    if (!o.contains(key)) return false;
    const json& v = o[key];
    if (v.is_boolean()) { out = v.get<bool>(); return true; }
    if (v.is_string()) {
        std::string s = v.get<std::string>();
        if (s == "true")  { out = true;  return true; }
        if (s == "false") { out = false; return true; }
    }
    return false;
}

// 在父样式基础上叠加本组件声明的颜色与格式位
McText::Style resolveStyle(const json& o, const McText::Style& inherited) {
    McText::Style st = inherited;
    bool flag = false;
    if (readFlag(o, "bold", flag))          st.bold = flag;
    if (readFlag(o, "italic", flag))        st.italic = flag;
    if (readFlag(o, "underlined", flag))    st.underlined = flag;
    if (readFlag(o, "strikethrough", flag)) st.strikethrough = flag;
    if (readFlag(o, "obfuscated", flag))    st.obfuscated = flag;

    if (o.contains("color") && o["color"].is_string()) {
        std::string color = o["color"].get<std::string>();
        if (color == "reset" || color == "none") {
            st.hasColor = false;      // 回到默认色，格式位保留（原版语义）
        } else if (!McText::applyColorName(color, st)) {
            // 未知颜色名（含 "inherit"）沿用父样式
        }
    }
    return st;
}

// 翻译串按 %s / %d / %N$s / %N$d / %% 展开；每个字面段都重新声明父样式，
// 使带样式的参数之后模板余文不会继承参数的颜色
void appendTranslated(std::string& out, const std::string& fmt,
                      const std::vector<std::string>& args, const McText::Style& st) {
    const std::string codes = McText::styleToCodes(st);
    std::string lit;
    size_t nextArg = 0;
    size_t i = 0;

    auto flush = [&]() {
        if (lit.empty()) return;
        out += codes;
        out += lit;
        lit.clear();
    };

    while (i < fmt.size()) {
        char c = fmt[i];
        if (c != '%') { lit.push_back(c); ++i; continue; }

        size_t k = i + 1;
        if (k >= fmt.size()) { lit.push_back('%'); break; }        // 结尾孤立的 %
        if (fmt[k] == '%')  { lit.push_back('%'); i = k + 1; continue; }   // %% → 字面 %

        // 可选的 "N$" 位置前缀
        size_t digits = k;
        while (digits < fmt.size() && fmt[digits] >= '0' && fmt[digits] <= '9') ++digits;
        bool indexed = digits > k && digits < fmt.size() && fmt[digits] == '$';
        size_t pos = digits;
        size_t idx = 0;
        if (indexed) {
            long parsed = std::strtol(fmt.c_str() + k, nullptr, 10);
            idx = (parsed > 0) ? (size_t)(parsed - 1) : args.size();   // %0$s 视为越界
            pos += 1;                 // 跳过 $
        }

        char conv = (pos < fmt.size()) ? fmt[pos] : '\0';
        if (conv != 's' && conv != 'd') {   // 不是占位符（例如 "%1x"），% 原样保留
            lit.push_back('%');
            ++i;
            continue;
        }
        if (!indexed) idx = nextArg++;
        flush();
        if (idx < args.size()) out += args[idx];
        i = pos + 1;
    }
    flush();
}

void append(std::string& out, const json& node, const McText::Style& inherited,
            const TranslateResolver& resolver) {
    if (node.is_string()) {          // 纯字符串子组件：不加样式码，沿用父样式
        out += node.get<std::string>();
        return;
    }
    if (node.is_array()) {
        for (const json& child : node) append(out, child, inherited, resolver);
        return;
    }
    if (!node.is_object()) return;   // 数字/布尔等非法组件忽略

    McText::Style st = resolveStyle(node, inherited);
    const std::string codes = McText::styleToCodes(st);

    // 主体内容：translate 优先，解析不了回退 text
    std::string content;
    if (node.contains("translate") && node["translate"].is_string()) {
        const std::string* translated =
            resolver ? resolver(node["translate"].get<std::string>()) : nullptr;
        if (translated) {
            std::vector<std::string> args;
            if (node.contains("with") && node["with"].is_array()) {
                for (const json& arg : node["with"]) {
                    std::string sub;
                    append(sub, arg, st, resolver);
                    args.push_back(sub);
                }
            }
            appendTranslated(content, *translated, args, st);
        } else if (node.contains("text") && node["text"].is_string()) {
            content = node["text"].get<std::string>();
        }
    } else if (node.contains("text") && node["text"].is_string()) {
        content = node["text"].get<std::string>();
    }

    if (!content.empty()) {
        out += codes;
        out += content;
    }

    if (node.contains("extra") && node["extra"].is_array()) {
        for (const json& child : node["extra"]) {
            std::string sub;
            append(sub, child, st, resolver);
            out += sub;
        }
        out += codes;   // 子组件可能改了样式，这里恢复本组件样式供后续兄弟使用
    }
}

} // namespace

std::string toText(const json& root, const TranslateResolver& resolver) {
    std::string out;
    McText::Style base;
    append(out, root, base, resolver);
    return out;
}
}
