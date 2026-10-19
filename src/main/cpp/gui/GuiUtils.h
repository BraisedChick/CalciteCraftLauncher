#pragma once

#include "imgui.h"
#include "McTextFormat.h"
#include "ResourcepackManager.h"
#include "MusicManager.h"
#include "ClientEngine/ClientEngine.h"
#include "Renderer/VulkanRenderer.h"
#include "GameUI.h"
#include <string>
#include <cstdint>

// GUI 共享工具（§ 格式文本的解析/绘制见 McTextFormat.h）

// ===== MC 风格九宫格按钮 =====

// 按钮纹理即时获取（不缓存 ID，GL 由 ResourcepackManager、Vulkan 由 VulkanRenderer 统一管理缓存）
// GL 后端：ImTextureID = GL 纹理 ID；Vulkan 后端：ImTextureID = ImGui_ImplVulkan_AddTexture 返回的 VkDescriptorSet
inline void ensureWidgetTextures(ImTextureID& button, ImTextureID& buttonHighlighted, ImTextureID& buttonDisabled) {
    if (GameUI::getInstance().isVulkanBackend()) {
        auto* vk = ClientEngine::getInstance() ? ClientEngine::getInstance()->getVulkanRenderer() : nullptr;
        if (!vk) {
            button = buttonHighlighted = buttonDisabled = 0;
            return;
        }
        button = (ImTextureID)(intptr_t)vk->getGuiTexture("sprites/widget/button");
        buttonHighlighted = (ImTextureID)(intptr_t)vk->getGuiTexture("sprites/widget/button_highlighted");
        buttonDisabled = (ImTextureID)(intptr_t)vk->getGuiTexture("sprites/widget/button_disabled");
        return;
    }
    auto& rm = ResourcepackManager::getInstance();
    button = (ImTextureID)(intptr_t)rm.getGuiTexture("sprites/widget/button");
    buttonHighlighted = (ImTextureID)(intptr_t)rm.getGuiTexture("sprites/widget/button_highlighted");
    buttonDisabled = (ImTextureID)(intptr_t)rm.getGuiTexture("sprites/widget/button_disabled");
}

// 物品图标（hotbar/物品栏，双后端）：GL 走 ResourcepackManager（3D 图标缓存+2D 回退），
// Vulkan 走 VulkanRenderer::getItemTexture（CPU 光栅化 3D 图标+2D 回退）
inline ImTextureID getItemIconTexture(const std::string& itemName) {
    if (GameUI::getInstance().isVulkanBackend()) {
        auto* vk = ClientEngine::getInstance() ? ClientEngine::getInstance()->getVulkanRenderer() : nullptr;
        return vk ? (ImTextureID)(intptr_t)vk->getItemTexture(itemName) : 0;
    }
    return (ImTextureID)(intptr_t)ResourcepackManager::getInstance().getItemTexture(itemName);
}

// GUI 纹理（gui/<path>.png，如容器背景，双后端）
inline ImTextureID getGuiTextureId(const std::string& path) {
    if (GameUI::getInstance().isVulkanBackend()) {
        auto* vk = ClientEngine::getInstance() ? ClientEngine::getInstance()->getVulkanRenderer() : nullptr;
        return vk ? (ImTextureID)(intptr_t)vk->getGuiTexture(path) : 0;
    }
    return (ImTextureID)(intptr_t)ResourcepackManager::getInstance().getGuiTexture(path);
}

// HUD 纹理（gui/sprites/hud/<path>.png，心/食物/经验条，双后端）
inline ImTextureID getHudTextureId(const std::string& path) {
    if (GameUI::getInstance().isVulkanBackend()) {
        auto* vk = ClientEngine::getInstance() ? ClientEngine::getInstance()->getVulkanRenderer() : nullptr;
        return vk ? (ImTextureID)(intptr_t)vk->getAssetTexture("gui/sprites/hud/" + path + ".png") : 0;
    }
    return (ImTextureID)(intptr_t)ResourcepackManager::getInstance().getHudTexture(path);
}

// GL 解绑 sampler 回退纹理自身 NEAREST 参数；Vulkan 切到 ImGui 后端的 NEAREST 采样器描述符
// （二者都切到 NEAREST 且持续到本帧后续 draw，像素画 HUD/物品图标不再被 Linear 采样糊化）
inline void addNearestSamplerCallback(ImDrawList* dl) {
    if (GameUI::getInstance().isVulkanBackend()) {
        // imgui_impl_vulkan 初始化时注册的标准回调，绑定 NEAREST 采样器描述符集（set 1）
        if (auto cb = ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest) {
            dl->AddCallback(cb, nullptr);
        }
    } else {
        dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
            glBindSampler(0, 0);
        }, nullptr);
    }
}

// 九宫格绘制（MC .mcmeta nine_slice 规范）
// texW/texH = 纹理像素尺寸，border = 边框像素宽度
inline void drawNineSlice(ImDrawList* dl, ImTextureID tex,
    ImVec2 pos, ImVec2 size,
    float texW, float texH, float border)
{
    if (tex == 0) return;
    // GL 解绑 sampler 回退纹理自身 NEAREST 参数；Vulkan 用后端默认 Linear sampler
    // （1:1 边框 + 平色拉伸区域下两种采样视觉等价，不切 Nearest 可避免回调打断合批）
    if (!GameUI::getInstance().isVulkanBackend()) {
        dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
            glBindSampler(0, 0);
        }, nullptr);
    }

    float u[4] = { 0, border / texW, (texW - border) / texW, 1.0f };
    float v[4] = { 0, border / texH, (texH - border) / texH, 1.0f };
    float sx[4] = { pos.x, pos.x + border, pos.x + size.x - border, pos.x + size.x };
    float sy[4] = { pos.y, pos.y + border, pos.y + size.y - border, pos.y + size.y };

    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            dl->AddImage(tex,
                ImVec2(sx[c], sy[r]), ImVec2(sx[c + 1], sy[r + 1]),
                ImVec2(u[c], v[r]), ImVec2(u[c + 1], v[r + 1]));
        }
    }
}

// MC 风格按钮：九宫格纹理 + 居中文字 + 黑色阴影
// 返回 true 当按钮被点击（与 ImGui::Button 相同语义）
inline bool McButton(const char* label, ImVec2 size, bool enabled = true) {
    ImTextureID btn, btnHl, btnDis;
    ensureWidgetTextures(btn, btnHl, btnDis);

    // 不可见按钮做点击/悬停检测
    bool clicked = false;
    if (!enabled) ImGui::BeginDisabled();
    clicked = ImGui::InvisibleButton(label, size);
    if (!enabled) ImGui::EndDisabled();

    // 根据状态选择纹理
    ImTextureID tex;
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

    // 九宫格绘制纹理（200x20, 3px border）；纹理不可用时纯色矩形回退
    ImVec2 pos = ImGui::GetItemRectMin();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (tex != 0) {
        drawNineSlice(dl, tex, pos, size, 200.0f, 20.0f, 3.0f);
    } else {
        ImU32 bg;
        if (!enabled) bg = IM_COL32(40, 40, 40, 255);
        else if (ImGui::IsItemActive() || ImGui::IsItemHovered()) bg = IM_COL32(90, 90, 120, 255);
        else bg = IM_COL32(60, 60, 60, 255);
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg);
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(0, 0, 0, 200));
    }

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
