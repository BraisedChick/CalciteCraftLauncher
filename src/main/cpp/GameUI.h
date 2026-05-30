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

    // 多点触控入口（由 JNI 路由调用）
    void onTouchEvent(int pointerId, float x, float y, int action);

    // 键盘输入（来自 Java JNI）
    void addInputCharacter(unsigned int c);
    bool wantsTextInput();

    // 连接回调
    using ConnectCallback = std::function<void(const std::string& ip, int port)>;
    void setConnectCallback(ConnectCallback cb) { connectCallback = cb; }

    // 退出游戏回调（返回 Java 启动器）
    using ExitCallback = std::function<void()>;
    void setExitCallback(ExitCallback cb) { exitCallback = cb; }

    bool isInitialized() const { return initialized; }

private:
    GameUI() = default;
    ~GameUI() = default;

    struct ServerInfo {
        std::string name;
        std::string ip;
        int port = 25565;
    };

    void renderMainMenu();
    void renderMultiplayer();
    void renderAddServer();
    void renderConnecting();
    void renderInGameUI();
    void processTouchEvents();

    void loadServerList();
    void saveServerList();
    void connectToServer(const ServerInfo& server);

    // 多点触控
    struct TouchPoint {
        int id = -1;
        bool active = false;
        enum Role { NONE, JOYSTICK, CAMERA, UP_BUTTON, DOWN_BUTTON, SPRINT_BUTTON };
        Role role = NONE;
        float cameraLastX = 0, cameraLastY = 0;
    };
    static const int MAX_TOUCH_POINTS = 6;
    TouchPoint touchPoints[MAX_TOUCH_POINTS];

    TouchPoint* findTouchPoint(int id);
    TouchPoint* allocTouchPoint(int id);
    void freeTouchPoint(int id);
    bool isRoleTaken(TouchPoint::Role role) const;

    void handleJoystickTouch(int pointerId, float x, float y, int action);
    void handleCameraTouch(int pointerId, float x, float y, int action);
    bool isInJoystickArea(float x, float y) const;
    bool isInUpButtonArea(float x, float y) const;
    bool isInDownButtonArea(float x, float y) const;
    bool isInSprintButtonArea(float x, float y) const;

    UIState currentState = UIState::MAIN_MENU;
    ConnectCallback connectCallback;
    ExitCallback exitCallback;
    std::string connectingAddress;

    // 服务器列表
    std::vector<ServerInfo> servers;
    int selectedServer = -1;
    bool showingAddServer = false;
    int editingServerIndex = -1;
    char addServerName[64] = "";
    char addServerIp[64] = "";
    char addServerPort[16] = "25565";

    bool initialized = false;

    struct TouchEvent {
        float x, y;
        int action;
    };
    std::vector<TouchEvent> touchEvents;
    std::mutex touchMutex;

    // 游戏内 UI 状态
    struct {
        bool active = false;
        float knobX = 0, knobY = 0;
    } joystick;

    struct {
        bool upPressed = false;
        bool downPressed = false;
        bool sprintPressed = false;
    } buttons;
};
