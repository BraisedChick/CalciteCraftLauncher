#pragma once

#include <string>
#include <functional>
#include <vector>
#include <mutex>
#include <GLES3/gl3.h>
#include "Raycast.h"

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
    ConnectCallback getConnectCallback() const { return connectCallback; }

    // 退出游戏回调（返回 Java 启动器）
    using ExitCallback = std::function<void()>;
    void setExitCallback(ExitCallback cb) { exitCallback = cb; }
    ExitCallback getExitCallback() const { return exitCallback; }

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
    void setOptionsFov(float fov) { optionsFov = fov; }

    // 全景背景纹理 ID（由 GLRenderer 每帧设置）
    void setPanoramaTexture(GLuint texID) { panoramaTextureID = texID; }

    // EGL context 丢失时重置 GL 资源
    void resetGLResources() {
        panoramaTextureID = 0;
    }

    // 视频设置
    void setVideoSettingsOpen(bool open) { videoSettingsOpen = open; }
    bool isVideoSettingsOpen() const { return videoSettingsOpen; }
    int getRenderDistance() const { return renderDistance; }
    bool isSmoothLightingEnabled() const { return smoothLightingEnabled; }
    bool isMipmapEnabled() const { return mipmapLevel > 0; }
    int getMipmapLevel() const { return mipmapLevel; }
    int getMaxFps() const { return maxFps; }

    // 直接修改设置（供 PauseScreen 使用）
    void setRenderDistanceDirect(int v) { renderDistance = v; }
    void setSmoothLightingDirect(bool v) { smoothLightingEnabled = v; }
    void setMipmapDirect(bool v) { mipmapLevel = v ? 4 : 0; }
    void setMipmapLevelDirect(int v) { mipmapLevel = v; }
    void setMaxFpsDirect(int v) { maxFps = v; }

    // FOV 更新回调
    using FovCallback = std::function<void(float)>;
    void setFovCallback(FovCallback cb) { fovCallback = cb; }
    FovCallback fovCallback;

    // 渲染距离回调
    using RenderDistanceCallback = std::function<void(int)>;
    void setRenderDistanceCallback(RenderDistanceCallback cb) { renderDistanceCallback = cb; }
    RenderDistanceCallback renderDistanceCallback;

    // Mipmap 回调
    using MipmapCallback = std::function<void(int)>;
    void setMipmapCallback(MipmapCallback cb) { mipmapCallback = cb; }
    MipmapCallback getMipmapCallback() const { return mipmapCallback; }
    MipmapCallback mipmapCallback;

    // 最大帧率回调
    using MaxFpsCallback = std::function<void(int)>;
    void setMaxFpsCallback(MaxFpsCallback cb) { maxFpsCallback = cb; }
    MaxFpsCallback maxFpsCallback;

    // F3 调试信息
    void toggleDebugInfo() { showDebugInfo = !showDebugInfo; }
    bool isDebugInfoVisible() const { return showDebugInfo; }

    // 死亡界面
    void setDeathScreenActive(bool active) {
        deathScreenActive = active;
        if (!active) deathReason.clear();
    }
    bool isDeathScreenActive() const { return deathScreenActive; }
    void setDeathReason(const std::string& reason) { deathReason = reason; }

    // 背包界面
    bool isInventoryOpen() const { return inventoryOpen; }
    void setInventoryOpen(bool open) { inventoryOpen = open; }

    // 容器交互（由 ClientEngine 调用）
    void openContainer(int containerId, int containerType);
    void closeContainer();
    int getOpenContainerId() const { return openContainerId; }
    int getOpenContainerType() const { return openContainerType; }

    // HUD 按钮/摇杆状态 getter（供 HudScreen 读取高亮状态）
    bool isButtonUp() const { return buttons.upPressed; }
    bool isButtonDown() const { return buttons.downPressed; }
    bool isButtonSprint() const { return buttons.sprintPressed; }
    bool isButtonAttack() const { return buttons.attackPressed; }
    bool isButtonPlace() const { return buttons.placePressed; }
    bool isJoystickActive() const { return joystick.active; }
    float getJoystickKnobX() const { return joystick.knobX; }
    float getJoystickKnobY() const { return joystick.knobY; }

    // 挖掘状态（供 GLRenderer 渲染破坏覆盖层）
    bool isDigging() const { return digging; }
    int getDigBlockX() const { return digBlockX; }
    int getDigBlockY() const { return digBlockY; }
    int getDigBlockZ() const { return digBlockZ; }
    int getDestroyStage() const {
        if (destroyProgress <= 0.0f) return -1;
        int stage = (int)(destroyProgress * 10.0f);
        if (stage > 9) stage = 9;
        if (stage < 0) stage = 0;
        return stage;
    }

    // 是否有任意游戏内界面打开（阻挡移动输入）
    bool isInGameUIActive() const {
        return gameMenuOpen || optionsOpen || deathScreenActive || inventoryOpen;
    }

    // 保存设置
    void saveSettingsNow();

    // 连接地址（供 ConnectingScreen 使用）
    void setConnectingAddress(const std::string& addr) { connectingAddress = addr; }
    std::string getConnectingAddress() const { return connectingAddress; }

private:
    GameUI() = default;
    ~GameUI() = default;

    void updateOverlays();
    void processTouchEvents();
    void loadSettings();
    void saveSettings();

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
    void continueDestroyBlock();
    void stopDestroyBlock();
    RaycastResult getTargetBlock() const;
    bool isInJoystickArea(float x, float y) const;
    bool isInUpButtonArea(float x, float y) const;
    bool isInDownButtonArea(float x, float y) const;
    bool isInSprintButtonArea(float x, float y) const;
    bool isInAttackButtonArea(float x, float y) const;
    bool isInPlaceButtonArea(float x, float y) const;
    bool isInF3ButtonArea(float x, float y) const;
    bool isInEButtonArea(float x, float y) const;
    int hotbarSlotAt(float x, float y) const;

    UIState currentState = UIState::MAIN_MENU;
    ConnectCallback connectCallback;
    ExitCallback exitCallback;
    DisconnectCallback disconnectCallback;
    std::string connectingAddress;

    bool initialized = false;
    GLuint panoramaTextureID = 0;

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
    bool mipmapEnabled = true; // legacy compat
    int mipmapLevel = 4;
    int maxFps = 0;

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

    // 挖掘状态（对标原版 MultiPlayerGameMode）
    bool digging = false;             // isDestroying
    int digBlockX = 0, digBlockY = 0, digBlockZ = 0;  // destroyBlockPos
    int digFace = 0;
    float destroyProgress = 0.0f;     // 挖掘进度（0.0→1.0）
    float destroyAccumulator = 0.0f;  // 时间累加器（用于固定刻率 50ms）
    int destroyDelay = 0;             // 挖掘后冷却（刻数，每刻50ms）

    // 死亡界面状态
    bool deathScreenActive = false;
    std::string deathReason;
    float prevHealth = 20.0f;

    // 背包界面
    bool inventoryOpen = false;

    // 容器状态（由 OpenScreen 包设置）
    int openContainerId = -1;   // -1 = 无容器, 0 = 玩家背包, >0 = 外部容器
    int openContainerType = -1; // 菜单类型：11=工作台，等

    // overlay 跟踪（避免每帧重建）
    bool lastDeathActive = false;
    bool lastMenuOpen = false;
    bool lastInventoryOpen = false;
};
