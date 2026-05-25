#pragma once

#include <string>
#include <functional>
#include <vector>
#include <mutex>

enum class UIState {
    MAIN_MENU,
    MULTIPLAYER,
    CONNECTING,
    IN_GAME
};

class GameUI {
public:
    static GameUI& getInstance();

    bool init();
    void shutdown();
    void render();

    UIState getState() const { return currentState; }
    void setState(UIState state) { currentState = state; }

    // 触摸输入（来自 Java JNI）
    void queueTouchEvent(float x, float y, int action);

    // 键盘输入（来自 Java JNI）
    void addInputCharacter(unsigned int c);
    bool wantsTextInput();

    // 连接回调
    using ConnectCallback = std::function<void(const std::string& ip, int port)>;
    void setConnectCallback(ConnectCallback cb) { connectCallback = cb; }

    bool isInitialized() const { return initialized; }

private:
    GameUI() = default;
    ~GameUI() = default;

    void renderMainMenu();
    void renderMultiplayer();
    void renderConnecting();
    void processTouchEvents();

    UIState currentState = UIState::MAIN_MENU;
    ConnectCallback connectCallback;

    char ipBuffer[64] = "127.0.0.1";
    char portBuffer[16] = "25565";

    bool initialized = false;

    struct TouchEvent {
        float x, y;
        int action; // 0=down, 1=up, 2=move
    };
    std::vector<TouchEvent> touchEvents;
    std::mutex touchMutex;
};
