#pragma once

#include <functional>
#include <string>

#include "3rdparty/json.hpp"

// 原版 Chat Component JSON → 带 § 格式代码的文本，输出交给 McText 渲染
// 取代原先散落在 GameEngine（丢样式）与 ServerList（只认颜色且近似到 16 色）的两套解析
namespace ChatComponent {

// 翻译 key → 本地化字符串；返回 nullptr 表示没有该翻译。不传则 translate 字段整体回退
using TranslateResolver = std::function<const std::string*(const std::string&)>;

// root 可以是组件对象、组件数组或纯字符串。样式按父子继承：
// 子组件未声明的颜色/格式位沿用父组件，子组件结束后恢复父组件样式
std::string toText(const nlohmann::json& root, const TranslateResolver& resolver = nullptr);
}
