#pragma once

#include <string>
#include <deque>
#include <functional>

// 聊天界面（消息显示 + 输入框），从 GameUI 中分离出来
// 由 GameUI 持有并在游戏内每帧渲染；消息由网络层经 GameUI 转发进来
// 依赖通过回调注入，不直接依赖 ClientEngine/GameEngine 单例
class ChatScreen {
public:
    struct ChatEntry {
        std::string text;
        unsigned int color = 0xFFFFFFFF;  // RGBA
    };

    // 发送消息回调（注入：最终调用 GameEngine::sendChatMessage）
    using SendCallback = std::function<void(const std::string&)>;
    void setSendCallback(SendCallback cb) { sendCallback = cb; }

    // 设置聊天字体（ImFont* 以 void* 传入，避免头文件依赖 imgui）
    void setFont(void* font) { chatFontPtr = font; }

    // 添加一条消息（网络层收到聊天包时调用）
    void addMessage(const std::string& text, unsigned int color = 0xFFFFFFFF);

    // 清空消息（进入服务器时调用）
    void clear() { messages.clear(); }

    // 切换输入框开关（T 按钮）
    void toggle();
    bool isOpen() const { return chatOpen; }

    // 每帧渲染：消息列表 + 输入框
    void render();

private:
    void sendMessage();

    bool chatOpen = false;
    char chatInput[256] = {};
    std::deque<ChatEntry> messages;
    void* chatFontPtr = nullptr;
    double chatLastMsgTime = 0.0;
    SendCallback sendCallback;
    // 是否需要一次性自动滚到底部（新消息到来或打开聊天时置 true，触发一次后清 false）
    bool chatNeedAutoScroll = false;
    // 累计滚动偏移（拖动时累加，每帧通过 SetNextWindowScroll 应用到窗口）
    float chatScrollOffset = 0.0f;
};
