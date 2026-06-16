#pragma once

#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <chrono>
#include <GLES3/gl3.h>

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
    bool isSmoothLightingEnabled() const { return smoothLightingEnabled; }
    bool isMipmapEnabled() const { return mipmapEnabled; }

    // FOV 更新回调
    using FovCallback = std::function<void(float)>;
    void setFovCallback(FovCallback cb) { fovCallback = cb; }
    FovCallback fovCallback;

    // 渲染距离回调
    using RenderDistanceCallback = std::function<void(int)>;
    void setRenderDistanceCallback(RenderDistanceCallback cb) { renderDistanceCallback = cb; }
    RenderDistanceCallback renderDistanceCallback;

    // Mipmap 回调
    using MipmapCallback = std::function<void(bool)>;
    void setMipmapCallback(MipmapCallback cb) { mipmapCallback = cb; }
    MipmapCallback getMipmapCallback() const { return mipmapCallback; }
    MipmapCallback mipmapCallback;

    // F3 调试信息
    void toggleDebugInfo() { showDebugInfo = !showDebugInfo; }
    bool isDebugInfoVisible() const { return showDebugInfo; }

    // 死亡界面
    void setDeathScreenActive(bool active) { deathScreenActive = active; }
    bool isDeathScreenActive() const { return deathScreenActive; }
    void setDeathReason(const std::string& reason) { deathReason = reason; }

    // 背包界面
    bool isInventoryOpen() const { return inventoryOpen; }

    // 是否有任意游戏内界面打开（阻挡移动输入）
    bool isInGameUIActive() const {
        return gameMenuOpen || optionsOpen || deathScreenActive || inventoryOpen;
    }

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
    void renderDeathScreen();
    void renderInventory();
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
        enum Role { NONE, JOYSTICK, CAMERA, UP_BUTTON, DOWN_BUTTON, SPRINT_BUTTON, ATTACK_BUTTON, PLACE_BUTTON, F3_BUTTON, E_BUTTON };
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
    void performBlockPlacement();
    void performBlockBreak();
    bool isInJoystickArea(float x, float y) const;
    bool isInUpButtonArea(float x, float y) const;
    bool isInDownButtonArea(float x, float y) const;
    bool isInSprintButtonArea(float x, float y) const;
    bool isInAttackButtonArea(float x, float y) const;
    bool isInPlaceButtonArea(float x, float y) const;
    bool isInF3ButtonArea(float x, float y) const;
    // E 按钮区域（打开背包）
    bool isInEButtonArea(float x, float y) const;
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
    bool smoothLightingEnabled = true;
    bool mipmapEnabled = true;

    // 游戏内 UI 状态
    struct {
        bool active = false;
        float knobX = 0, knobY = 0;
    } joystick;

    struct {
        bool upPressed = false;
        bool downPressed = false;
        bool sprintPressed = false;
        bool attackPressed = false;
        bool placePressed = false;
    } buttons;

    bool showDebugInfo = false;

    // 挖掘中断追踪
    bool digging = false;
    int digBlockX = 0, digBlockY = 0, digBlockZ = 0;
    int digFace = 0;
    std::chrono::steady_clock::time_point digStartTime;
    float digDuration = 0.0f;  // 挖掘所需时间（秒）

    // HUD 纹理缓存（初始化时加载，避免每帧查询）
    GLuint texHeartContainer = 0;
    GLuint texHeartFull = 0;
    GLuint texHeartHalf = 0;
    GLuint texFoodEmpty = 0;
    GLuint texFoodFull = 0;
    GLuint texFoodHalf = 0;
    GLuint texExpBarBg = 0;
    GLuint texExpBarProgress = 0;
    bool hudTexturesLoaded = false;

    // 死亡界面状态
    bool deathScreenActive = false;
    std::string deathReason;
    float prevHealth = 20.0f;

    // 背包界面
    bool inventoryOpen = false;

    // 背包拖拽（Quick Craft）状态
    // 原版MC的拖拽流程：按住鼠标拖过多个格子 → 松开时均分物品
    // status: 0=未开始, 1=拖拽中, 2=结束（分发）
    int quickcraftStatus = 0;
    std::vector<int> quickcraftSlots;  // 拖拽经过的槽位索引列表
    int quickcraftStartSlot = -1;      // 拖拽起始槽位
    bool isDraggingSlot = false;       // 是否正在拖拽
};
