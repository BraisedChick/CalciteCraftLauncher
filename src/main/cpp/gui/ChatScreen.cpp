#include "ChatScreen.h"

#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <cstring>

void ChatScreen::addMessage(const std::string& text, unsigned int color) {
    messages.push_back({text, color});
    if (messages.size() > 100) {
        messages.pop_front();
    }
    chatLastMsgTime = ImGui::GetTime();
    // 新消息到来：标记需要自动滚到底部（一次性动作，不每帧强制）
    chatNeedAutoScroll = true;
}

void ChatScreen::toggle() {
    if (chatOpen) {
        chatOpen = false;
    } else {
        chatOpen = true;
        memset(chatInput, 0, sizeof(chatInput));
        // 打开聊天时标记需要自动滚到底部
        chatNeedAutoScroll = true;
    }
}

void ChatScreen::sendMessage() {
    if (chatInput[0] == '\0') {
        chatOpen = false;
        return;
    }
    if (sendCallback) {
        sendCallback(std::string(chatInput));
    }
    chatOpen = false;
    chatInput[0] = '\0';
}

void ChatScreen::render() {
    // ===== 聊天消息显示 =====
    if (!messages.empty()) {
        // 5 秒无新消息自动隐藏
        double elapsed = ImGui::GetTime() - chatLastMsgTime;
        bool showChat = chatOpen || elapsed < 5.0;
        if (showChat) {
            // 淡出效果：最后 0.5 秒渐变透明
            float alpha = 1.0f;
            if (!chatOpen && elapsed > 4.5f) {
                alpha = (5.0f - (float)elapsed) / 0.5f;
                if (alpha < 0.0f) alpha = 0.0f;
            }

            ImGuiIO& io = ImGui::GetIO();
            float chatW = io.DisplaySize.x * 0.4f;
            float chatH = io.DisplaySize.y * 0.35f;
            float chatX = 10.0f;
            float chatY = io.DisplaySize.y - chatH - io.DisplaySize.y * 0.15f;

            // 聊天消息背景
            int bgAlpha = (int)(120 * alpha);
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                ImVec2(chatX, chatY),
                ImVec2(chatX + chatW, chatY + chatH),
                IM_COL32(0, 0, 0, bgAlpha), 4.0f);

            // 显示最近的 20 条消息（拖动消息区域即可滚动查看历史）
            ImGui::SetNextWindowPos(ImVec2(chatX + 4, chatY + 4));
            ImGui::SetNextWindowScroll(ImVec2(0.0f, chatScrollOffset));
            ImGuiWindowFlags chatFlags =
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoBackground;
            ImGui::Begin("##ChatDisplay", nullptr, chatFlags);

            int start = std::max(0, (int)messages.size() - 20);
            if (chatFontPtr) ImGui::PushFont((ImFont*)chatFontPtr);
            for (int i = start; i < (int)messages.size(); i++) {
                const auto& entry = messages[i];
                ImVec4 col(
                    ((entry.color >> 0) & 0xFF) / 255.0f,
                    ((entry.color >> 8) & 0xFF) / 255.0f,
                    ((entry.color >> 16) & 0xFF) / 255.0f,
                    ((entry.color >> 24) & 0xFF) / 255.0f * alpha
                );
                ImGui::TextColored(col, "%s", entry.text.c_str());
            }
            if (chatFontPtr) ImGui::PopFont();
            ImGui::SetWindowSize(ImVec2(chatW - 8, chatH - 8));

            // 拖动消息区域滚动（手指/鼠标在聊天背景区域内按住拖动即可）
            ImVec2 mousePos = ImGui::GetMousePos();
            bool mouseInChat = mousePos.x >= chatX && mousePos.x <= chatX + chatW &&
                               mousePos.y >= chatY && mousePos.y <= chatY + chatH;
            bool userDragging = mouseInChat && ImGui::IsMouseDragging(0, 1.0f);
            if (userDragging) {
                // 直接用本帧鼠标位移（ImGui 内部算好的，比 GetMouseDragDelta 差分可靠）
                float dy = ImGui::GetIO().MouseDelta.y;
                if (dy != 0.0f) {
                    // 向下拖(dy>0) = 看上方旧消息 = scroll↓；向上拖(dy<0) = 看下方新消息 = scroll↑
                    chatScrollOffset -= dy;
                    // 用户主动拖动：取消自动滚，直到新消息到来
                    chatNeedAutoScroll = false;
                }
            }

            // 自动滚到底部（新消息到来时持续尝试，直到用户拖动）
            // 每帧都调用 SetScrollHereY，确保布局更新后能滚到真正底部
            if (chatNeedAutoScroll && !userDragging) {
                ImGui::SetScrollHereY(1.0f);  // 把最后一个 item 对齐到窗口底部
                chatScrollOffset = ImGui::GetScrollMaxY();  // 每帧都同步，逐渐接近正确值
            }
            
            // 用户拖动时取消自动滚
            if (userDragging) {
                chatNeedAutoScroll = false;
            }

            // 将 scroll 限制在合法范围内（内容可能还没渲染完，maxY 会动态变化）
            float maxY = ImGui::GetScrollMaxY();
            if (chatScrollOffset < 0.0f) chatScrollOffset = 0.0f;
            if (chatScrollOffset > maxY) chatScrollOffset = maxY;

            ImGui::End();
        }
    }

    // ===== 聊天输入框（T 键触发）=====
    if (chatOpen) {
        ImGuiIO& io = ImGui::GetIO();
        float inputW = io.DisplaySize.x * 0.8f;
        float inputX = (io.DisplaySize.x - inputW) * 0.5f;
        float inputY = io.DisplaySize.y * 0.1f;

        ImGui::SetNextWindowPos(ImVec2(inputX, inputY));
        ImGui::SetNextWindowSize(ImVec2(inputW, 60.0f));
        ImGui::Begin("##ChatInput", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::PushItemWidth(-1);
        if (chatFontPtr) ImGui::PushFont((ImFont*)chatFontPtr);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.08f, 0.12f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

        // InputText with Enter handler
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;

        if (ImGui::InputText("##ChatMsg", chatInput, 256, flags)) {
            sendMessage();
        }

        ImGui::PopStyleColor(2);
        if (chatFontPtr) ImGui::PopFont();
        ImGui::PopItemWidth();

        // 点击输入框外部关闭聊天（有活动项时不关闭，如正在编辑输入框）
        if (!ImGui::IsAnyItemActive() &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
            ImGui::IsMouseClicked(0)) {
            chatOpen = false;
        }

        ImGui::End();
    }
}
