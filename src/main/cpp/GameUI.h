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

    // 断开连接回调（返回服务器列表）
    using DisconnectCallback = std::function<void()>;
    void setDisconnectCallback(DisconnectCallback cb) { disconnectCallback = cb; }

    bool isInitialized() const { return initialized; }

    // 游戏内菜单
    void setGameMenuOpen(bool open) { gameMenuOpen = open; }
    bool isGameMenuOpen() const { return gameMenuOpen; }

    // 选项界面
    void setOptionsOpen(bool open) { optionsOpen = open; }
    bool isOptionsOpen() const { return optionsOpen; }
    float getOptionsFov() const { return optionsFov; }

    // 视频设置
    void setVideoSettingsOpen(bool open) { videoSettingsOpen = open; }
    bool isVideoSettingsOpen() const { return videoSettingsOpen; }
    int getRenderDistance() const { return renderDistance; }

    // FOV 更新回调
    using FovCallback = std::function<void(float)>;
    void setFovCallback(FovCallback cb) { fovCallback = cb; }
    FovCallback fovCallback;

    // 渲染距离回调
    using RenderDistanceCallback = std::function<void(int)>;
    void setRenderDistanceCallback(RenderDistanceCallback cb) { renderDistanceCallback = cb; }
    RenderDistanceCallback renderDistanceCallback;

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
    void renderInGameMenu();
    void renderGameOptions();
    void renderVideoSettings();
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
    // 快捷栏点击检测：返回槽位索引 (0-8)，不在快捷栏区域则返回 -1
    int hotbarSlotAt(float x, float y) const;

    UIState currentState = UIState::MAIN_MENU;
    ConnectCallback connectCallback;
    ExitCallback exitCallback;
    DisconnectCallback disconnectCallback;
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

    // 游戏内菜单状态
    bool gameMenuOpen = false;
    bool optionsOpen = false;
    float optionsFov = 70.0f;
    bool videoSettingsOpen = false;
    int renderDistance = 10;

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
