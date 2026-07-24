#include "GameUI.h"
#include <android/log.h>
#include <android/asset_manager.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <unordered_set>
#include <algorithm>

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "CameraController.h"
#include "EntityManager.h"
#include "Collision.h"
#include "PlayerInventory.h"
#include "GLRenderer.h"
#include "ClientEngine/ClientEngine.h"
#include "ClientEngine/GameEngine.h"
#include "JniBridge.h"
#include "Raycast.h"
#include "ChunkManager.h"
#include "BlockRegistry.h"
#include "TextureLoader.h"
#include "MusicManager.h"

// Screen 系统（同目录）
#include "ScreenManager.h"
#include "TitleScreen.h"
#include "MultiplayerScreen.h"
#include "ConnectingScreen.h"
#include "HudScreen.h"
#include "DeathScreen.h"
#include "InventoryScreen.h"
#include "PauseScreen.h"
#include "ChatScreen.h"

#define LOG_TAG "GameUI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define GAMEKEY_W    0
#define GAMEKEY_S    1
#define GAMEKEY_A    2
#define GAMEKEY_D    3
#define GAMEKEY_UP   4
#define GAMEKEY_DOWN 5
#define GAMEKEY_SPRINT 6

#define OPTIONS_FILE_PATH "/data/data/com.calcite/options.txt"

#define JOYSTICK_CENTER_X      (ImGui::GetIO().DisplaySize.x * 0.1375f)
#define JOYSTICK_CENTER_Y_OFFSET (ImGui::GetIO().DisplaySize.y * 0.292f)
#define JOYSTICK_RADIUS        (std::min(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y) * 0.153f)
#define JOYSTICK_KNOB_RADIUS   (std::min(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y) * 0.053f)
#define JOYSTICK_MAX_DIST      (std::min(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y) * 0.09f)
#define BTN_RADIUS             (std::min(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y) * 0.053f)
#define BTN_RIGHT_MARGIN       (ImGui::GetIO().DisplaySize.x * 0.056f)
#define BTN_VERTICAL_SPACING   (ImGui::GetIO().DisplaySize.y * 0.118f)

GameUI& GameUI::getInstance() {
    static GameUI instance;
    return instance;
}

// ChatScreen 为不完全类型，析构函数定义在此（ChatScreen.h 已 include）
GameUI::~GameUI() = default;

bool GameUI::init() {
    if (initialized) return true;
    LOGI("Initializing ImGui...");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImFont* font = nullptr;

    // 从 APK assets 加载内置字体（兼容所有设备）
    {
        AAssetManager* am = TextureLoader::getAssetManager();
        if (am) {
            AAsset* asset = AAssetManager_open(am, "fonts/Minecraft.ttf", AASSET_MODE_BUFFER);
            if (asset) {
                const void* fontData = AAsset_getBuffer(asset);
                off_t fontLen = AAsset_getLength(asset);
                if (fontData && fontLen > 0) {
                    // 复制到持久内存（AAsset_close 后会释放原始 buffer）
                    void* persistentData = malloc((size_t)fontLen);
                    if (persistentData) {
                        memcpy(persistentData, fontData, (size_t)fontLen);
                        ImFont* f = io.Fonts->AddFontFromMemoryTTF(persistentData, (int)fontLen, 12.0f, nullptr,
                            io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
                        if (f) {
                            font = f;
                            LOGI("Loaded font from assets/fonts/Minecraft.ttf (%lld bytes)", (long long)fontLen);
                        }
                    }
                }
                AAsset_close(asset);
            }
        }
    }

    // 如果内置字体未加载，回退到系统字体
    static const char* fontPaths[] = {
        "/system/fonts/NotoSansSC-Regular.otf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
    };
    for (const char* path : fontPaths) {
        FILE* f = fopen(path, "rb");
        if (f) {
            fclose(f);
            font = io.Fonts->AddFontFromFileTTF(path, 18.0f, nullptr,
                io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
            if (font) { LOGI("Loaded CJK font: %s", path); break; }
        }
    }
    if (!font) {
        font = io.Fonts->AddFontDefault();
        LOGE("No CJK font found, Chinese text may display as boxes");
    }
    // 创建聊天界面并注入依赖（字体 + 发送回调）
    m_chat = std::make_unique<ChatScreen>();
    m_chat->setFont(font);
    m_chat->setSendCallback([](const std::string& msg) {
        auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
        if (engine) engine->sendChatMessage(msg);
    });

    io.FontGlobalScale = 2.0f;
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 4.0f;
    style.FrameBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.35f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.35f, 0.35f, 0.45f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.12f, 0.16f, 1.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);

    if (!ImGui_ImplOpenGL3_Init("#version 300 es")) {
        LOGE("Failed to initialize ImGui OpenGL3 backend");
        return false;
    }

    initialized = true;
    LOGI("ImGui initialized successfully");

    loadSettings();

    // ===== 注册 UI 回调（从 JNI 层迁移至此） =====

    // 断开连接回调
    setDisconnectCallback([]() {
        LOGI("In-game disconnect requested");
        auto* client = ClientEngine::getInstance();
        if (client && client->getGame()) {
            client->getGame()->disconnect();
        }
    });

    // 键盘显示回调（JNI 调用 Java 输入法）
    setShowKeyboardCallback([](bool show) {
        if (!JniBridge::getJvm() || !JniBridge::getActivity()) return;
        JNIEnv* env;
        bool attached = false;
        int ger = JniBridge::getJvm()->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (ger == JNI_EDETACHED) {
            if (JniBridge::getJvm()->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true;
            else return;
        } else if (ger != JNI_OK) {
            return;
        }
        jclass clazz = env->GetObjectClass(JniBridge::getActivity());
        jmethodID method = env->GetMethodID(clazz, "showKeyboardImGui", "(Z)V");
        if (method) {
            env->CallVoidMethod(JniBridge::getActivity(), method, show);
        }
        env->DeleteLocalRef(clazz);
        if (attached) JniBridge::detachCurrentThread();
    });

    // FOV 更新回调
    setFovCallback([](float fov) {
        auto* engine = ClientEngine::getInstance();
        if (engine && engine->getRenderer()) {
            engine->getRenderer()->setFov(fov);
        }
    });

    // 渲染距离更新回调
    setRenderDistanceCallback([](int chunks) {
        auto* engine = ClientEngine::getInstance();
        if (engine && engine->getRenderer()) {
            engine->getRenderer()->setRenderDistance(chunks);
        }
    });

    // Mipmap 更新回调
    setMipmapCallback([](int level) {
        auto* engine = ClientEngine::getInstance();
        if (engine && engine->getRenderer()) {
            engine->getRenderer()->setMipmapLevel(level);
        }
    });

    // 最大帧率回调
    setMaxFpsCallback([](int fps) {
        auto* engine = ClientEngine::getInstance();
        if (engine && engine->getRenderer()) {
            engine->getRenderer()->setMaxFps(fps);
        }
    });

    // 退出游戏回调（返回 Java 启动器）
    setExitCallback([]() {
        JNIEnv* env;
        bool attached = false;
        int getEnvResult = JniBridge::getJvm()->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (getEnvResult == JNI_EDETACHED) {
            if (JniBridge::getJvm()->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
            attached = true;
        } else if (getEnvResult != JNI_OK) {
            return;
        }
        jclass clazz = env->GetObjectClass(JniBridge::getActivity());
        jmethodID finishMethod = env->GetMethodID(clazz, "finish", "()V");
        if (finishMethod) {
            env->CallVoidMethod(JniBridge::getActivity(), finishMethod);
        }
        env->DeleteLocalRef(clazz);
        if (attached) {
            JniBridge::detachCurrentThread();
        }
    });

    // 设置应用已迁移至 native-lib.cpp 连接回调中（setRenderer 之后）
    // 因为此时渲染器由 g_pendingRenderer 暂存，ClientEngine 尚未创建

    ScreenManager::getInstance().setScreen(std::make_unique<TitleScreen>());

    return true;
}

void GameUI::shutdown() {
    if (!initialized) return;
    LOGI("Shutting down ImGui...");
    saveSettings();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    initialized = false;
}

void GameUI::queueTouchEvent(float x, float y, int action) {
    std::lock_guard<std::mutex> lock(touchMutex);
    touchEvents.push_back({x, y, action});
}

void GameUI::processTouchEvents() {
    std::vector<TouchEvent> events;
    {
        std::lock_guard<std::mutex> lock(touchMutex);
        events.swap(touchEvents);
    }
    ImGuiIO& io = ImGui::GetIO();
    for (const auto& e : events) {
        io.AddMousePosEvent(e.x, e.y);
        if (e.action == 0) io.AddMouseButtonEvent(0, true);
        else if (e.action == 1) io.AddMouseButtonEvent(0, false);
    }
}

void GameUI::addInputCharacter(unsigned int c) {
    if (!initialized) return;
    ImGuiIO& io = ImGui::GetIO();
    if (c == 127 || c == 8) {
        io.AddKeyEvent(ImGuiKey_Backspace, true);
        io.AddKeyEvent(ImGuiKey_Backspace, false);
    } else if (c == 10 || c == 13) {  // \n 或 \r → 回车键
        io.AddKeyEvent(ImGuiKey_Enter, true);
        io.AddKeyEvent(ImGuiKey_Enter, false);
    } else {
        io.AddInputCharacter(c);
    }
}

bool GameUI::wantsTextInput() {
    if (!initialized) return false;
    return ImGui::GetIO().WantTextInput;
}

// ===== ScreenManager 驱动的渲染 =====

void GameUI::render() {
    if (!initialized) return;
    processTouchEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    if (currentState == UIState::IN_GAME) {
        auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;

        // 挖掘后冷却递减（每刻 50ms）
        if (destroyDelay > 0) {
            destroyAccumulator += ImGui::GetIO().DeltaTime;
            while (destroyAccumulator >= 0.05f) {
                destroyAccumulator -= 0.05f;
                --destroyDelay;
                if (destroyDelay <= 0) break;
            }
        }

        // 持续按住攻击按钮时继续挖掘
        if (buttons.attackPressed && destroyDelay <= 0) {
            continueDestroyBlock();
        }
        float health = game ? game->getHealth() : 20.0f;
        if (!deathScreenActive && health <= 0.0f && prevHealth > 0.0f && game && game->getGameMode() != 3) {
            deathScreenActive = true;
            std::string serverMsg = game->getDeathMessage();
            deathReason = serverMsg.empty() ? "你被杀死了" : serverMsg;
        }
        prevHealth = health;
    }

    updateOverlays();
    ScreenManager::getInstance().render(0, 0);

    // ===== 聊天界面（消息显示 + 输入框，逻辑见 ChatScreen）=====
    if (currentState == UIState::IN_GAME && m_chat) {
        m_chat->render();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GameUI::updateOverlays() {
    auto& sm = ScreenManager::getInstance();

    if (currentState == UIState::IN_GAME) {
        if (!sm.hasScreen() || std::string(sm.getScreen()->getName()) != "HudScreen") {
            sm.setScreen(std::make_unique<HudScreen>());
        }

        if (deathScreenActive) {
            if (!lastDeathActive) {
                auto death = std::make_unique<DeathScreen>();
                death->setDeathReason(deathReason);
                death->setDisconnectCallback(disconnectCallback);
                sm.setOverlay(std::move(death));
                lastDeathActive = true;
                lastMenuOpen = false;
                lastInventoryOpen = false;
            }
        } else if (gameMenuOpen || optionsOpen) {
            if (!lastMenuOpen) {
                auto pause = std::make_unique<PauseScreen>();
                pause->setDisconnectCallback(disconnectCallback);
                pause->setFovCallback(fovCallback);
                pause->setRenderDistanceCallback(renderDistanceCallback);
                pause->setMipmapCallback(mipmapCallback);
                pause->setMaxFpsCallback(maxFpsCallback);
                pause->setSaveSettingsCallback([this]() { saveSettings(); });
                if (optionsOpen) {
                    pause->setSubPage(videoSettingsOpen ? PauseScreen::VIDEO_SETTINGS : PauseScreen::OPTIONS);
                }
                sm.setOverlay(std::move(pause));
                lastMenuOpen = true;
                lastDeathActive = false;
                lastInventoryOpen = false;
            }
        } else if (inventoryOpen) {
            if (!lastInventoryOpen) {
                sm.setOverlay(std::make_unique<InventoryScreen>());
                lastInventoryOpen = true;
                lastDeathActive = false;
                lastMenuOpen = false;
            }
        } else {
            if (sm.hasOverlay()) {
                sm.closeOverlay();
            }
            lastDeathActive = false;
            lastMenuOpen = false;
            lastInventoryOpen = false;
        }
    }
}

// ===== 设置持久化 =====

void GameUI::loadSettings() {
    std::ifstream file(OPTIONS_FILE_PATH);
    if (!file.is_open()) { LOGI("No options file found, using defaults"); return; }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;
        std::string key = line.substr(0, colonPos);
        std::string value = line.substr(colonPos + 1);
        try {
            if (key == "renderDistance") renderDistance = std::stoi(value);
            else if (key == "smoothLighting") smoothLightingEnabled = (value == "true");
            else if (key == "mipmap") {
                // 兼容旧版 bool 格式
                if (value == "true") mipmapLevel = 4;
                else if (value == "false") mipmapLevel = 0;
                else mipmapLevel = std::atoi(value.c_str());
            }
            else if (key == "maxFps") maxFps = std::stoi(value);
            else if (key == "fov") optionsFov = std::stof(value);
        } catch (...) {}
    }
    file.close();
    LOGI("Settings loaded: renderDistance=%d, fov=%.1f", renderDistance, optionsFov);
}

void GameUI::saveSettings() {
    std::ofstream file(OPTIONS_FILE_PATH, std::ios::trunc);
    if (!file.is_open()) { LOGE("Failed to save options"); return; }
    file << "renderDistance:" << renderDistance << '\n';
    file << "smoothLighting:" << (smoothLightingEnabled ? "true" : "false") << '\n';
    file << "mipmap:" << mipmapLevel << '\n';
    file << "maxFps:" << maxFps << '\n';
    file << "fov:" << optionsFov << '\n';
    file.close();
    LOGI("Settings saved");
}

void GameUI::saveSettingsNow() { saveSettings(); }

void GameUI::openContainer(int containerId, int containerType) {
    openContainerId = containerId;
    openContainerType = containerType;
    inventoryOpen = true;
    LOGI("Container opened: id=%d, type=%d", containerId, containerType);
}

void GameUI::closeContainer() {
    openContainerId = -1;
    openContainerType = -1;
    inventoryOpen = false;
}

// ===== 多点触控 =====

GameUI::TouchPoint* GameUI::findTouchPoint(int id) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++)
        if (touchPoints[i].active && touchPoints[i].id == id) return &touchPoints[i];
    return nullptr;
}

GameUI::TouchPoint* GameUI::allocTouchPoint(int id) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (!touchPoints[i].active) {
            touchPoints[i].active = true;
            touchPoints[i].id = id;
            touchPoints[i].role = TouchPoint::NONE;
            return &touchPoints[i];
        }
    }
    return nullptr;
}

void GameUI::freeTouchPoint(int id) {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) {
        if (touchPoints[i].active && touchPoints[i].id == id) {
            touchPoints[i].active = false;
            touchPoints[i].role = TouchPoint::NONE;
            return;
        }
    }
}

bool GameUI::isRoleTaken(TouchPoint::Role role) const {
    for (int i = 0; i < MAX_TOUCH_POINTS; i++)
        if (touchPoints[i].active && touchPoints[i].role == role) return true;
    return false;
}

void GameUI::onTouchEvent(int pointerId, float x, float y, int action) {
    // 聊天打开时，所有触摸直接路由给 ImGui（点击输入框才能聚焦）
    if (m_chat && m_chat->isOpen()) {
        queueTouchEvent(x, y, action);
        return;
    }
    if (currentState == UIState::IN_GAME && (gameMenuOpen || optionsOpen || deathScreenActive)) {
        queueTouchEvent(x, y, action);
        return;
    }
    if (currentState == UIState::IN_GAME && inventoryOpen) {
        if (isInEButtonArea(x, y) && action == 0) {
            // 关闭外部容器时发送关闭包
            if (openContainerId > 0) {
                auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
                if (game) game->sendContainerClose();
            }
            closeContainer();
            auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
            if (game && game->getCollision()) game->getCollision()->resetMovement();
            return;
        }
        queueTouchEvent(x, y, action);
        return;
    }

    if (action == 0) {
        auto* pt = allocTouchPoint(pointerId);
        if (!pt) return;

        if (!isRoleTaken(TouchPoint::JOYSTICK) && isInJoystickArea(x, y)) {
            pt->role = TouchPoint::JOYSTICK;
            handleJoystickTouch(pointerId, x, y, action);
        } else if (!isRoleTaken(TouchPoint::UP_BUTTON) && isInUpButtonArea(x, y)) {
            pt->role = TouchPoint::UP_BUTTON;
            auto* col = (ClientEngine::getInstance() && ClientEngine::getInstance()->getGame()) ? ClientEngine::getInstance()->getGame()->getCollision() : nullptr;
            if (col) col->setKeyState(GAMEKEY_UP, true);
            buttons.upPressed = true;
        } else if (!isRoleTaken(TouchPoint::DOWN_BUTTON) && isInDownButtonArea(x, y)) {
            pt->role = TouchPoint::DOWN_BUTTON;
            auto* col = (ClientEngine::getInstance() && ClientEngine::getInstance()->getGame()) ? ClientEngine::getInstance()->getGame()->getCollision() : nullptr;
            if (col) col->setKeyState(GAMEKEY_DOWN, true);
            buttons.downPressed = true;
        } else if (!isRoleTaken(TouchPoint::SPRINT_BUTTON) && isInSprintButtonArea(x, y)) {
            pt->role = TouchPoint::SPRINT_BUTTON;
            auto* col = (ClientEngine::getInstance() && ClientEngine::getInstance()->getGame()) ? ClientEngine::getInstance()->getGame()->getCollision() : nullptr;
            if (col) col->setKeyState(GAMEKEY_SPRINT, true);
            buttons.sprintPressed = !buttons.sprintPressed;
        } else if (!isRoleTaken(TouchPoint::ATTACK_BUTTON) && isInAttackButtonArea(x, y)) {
            pt->role = TouchPoint::ATTACK_BUTTON;
            buttons.attackPressed = true;
            performLeftClick();
        } else if (!isRoleTaken(TouchPoint::PLACE_BUTTON) && isInPlaceButtonArea(x, y)) {
            pt->role = TouchPoint::PLACE_BUTTON;
            buttons.placePressed = true;
            performRightClick();
        } else if (!isRoleTaken(TouchPoint::CHAT_BUTTON) && isInChatButtonArea(x, y)) {
            pt->role = TouchPoint::CHAT_BUTTON;
            openChat();
        } else if (!isRoleTaken(TouchPoint::F3_BUTTON) && isInF3ButtonArea(x, y)) {
            pt->role = TouchPoint::F3_BUTTON;
            toggleDebugInfo();
        } else if (!isRoleTaken(TouchPoint::E_BUTTON) && isInEButtonArea(x, y)) {
            pt->role = TouchPoint::E_BUTTON;
            if (inventoryOpen) {
                // 关闭外部容器时发送关闭包
                if (openContainerId > 0) {
                    auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
                    if (game) game->sendContainerClose();
                }
                closeContainer();
            } else {
                inventoryOpen = true;
            }
            auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
            if (game && game->getCollision()) game->getCollision()->resetMovement();
        } else if (currentState == UIState::IN_GAME) {
            int hbSlot = hotbarSlotAt(x, y);
            if (hbSlot >= 0) {
                auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
                if (game) {
                    game->getInventory()->setSelectedSlot(hbSlot);
                    game->sendHeldItemChange(hbSlot);
                }
            } else {
                pt->role = TouchPoint::CAMERA;
                handleCameraTouch(pointerId, x, y, action);
            }
        } else {
            pt->role = TouchPoint::CAMERA;
            handleCameraTouch(pointerId, x, y, action);
        }
    } else if (action == 2) {
        auto* pt = findTouchPoint(pointerId);
        if (!pt || !pt->active) return;
        switch (pt->role) {
            case TouchPoint::JOYSTICK: handleJoystickTouch(pointerId, x, y, action); break;
            case TouchPoint::CAMERA: handleCameraTouch(pointerId, x, y, action); break;
            default: break;
        }
    } else if (action == 1) {
        auto* pt = findTouchPoint(pointerId);
        if (!pt) return;
        switch (pt->role) {
            case TouchPoint::JOYSTICK: handleJoystickTouch(pointerId, x, y, action); break;
            case TouchPoint::CAMERA: break;
            case TouchPoint::UP_BUTTON: {
                auto* col2 = (ClientEngine::getInstance() && ClientEngine::getInstance()->getGame()) ? ClientEngine::getInstance()->getGame()->getCollision() : nullptr;
                if (col2) col2->setKeyState(GAMEKEY_UP, false);
                buttons.upPressed = false; break;
            }
            case TouchPoint::DOWN_BUTTON: {
                auto* col2 = (ClientEngine::getInstance() && ClientEngine::getInstance()->getGame()) ? ClientEngine::getInstance()->getGame()->getCollision() : nullptr;
                if (col2) col2->setKeyState(GAMEKEY_DOWN, false);
                buttons.downPressed = false; break;
            }
            case TouchPoint::SPRINT_BUTTON: break;
            case TouchPoint::ATTACK_BUTTON:
                buttons.attackPressed = false;
                stopDestroyBlock();
                break;
            case TouchPoint::PLACE_BUTTON:
                buttons.placePressed = false; break;
            default: break;
        }
        freeTouchPoint(pointerId);
    }
}

void GameUI::handleJoystickTouch(int pointerId, float x, float y, int action) {
    ImGuiIO& io = ImGui::GetIO();
    float jx = JOYSTICK_CENTER_X;
    float jy = io.DisplaySize.y - JOYSTICK_CENTER_Y_OFFSET;
    auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    auto* col = (game && game->getCollision()) ? game->getCollision() : nullptr;
    switch (action) {
        case 0: {
            joystick.active = true;
            float dx = x - jx, dy = y - jy;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > JOYSTICK_MAX_DIST) { dx = dx / dist * JOYSTICK_MAX_DIST; dy = dy / dist * JOYSTICK_MAX_DIST; }
            joystick.knobX = dx; joystick.knobY = dy;
            if (col) col->setJoystickInput(dx / JOYSTICK_MAX_DIST, dy / JOYSTICK_MAX_DIST);
            break;
        }
        case 2: {
            if (joystick.active) {
                float dx = x - jx, dy = y - jy;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist > JOYSTICK_MAX_DIST) { dx = dx / dist * JOYSTICK_MAX_DIST; dy = dy / dist * JOYSTICK_MAX_DIST; }
                joystick.knobX = dx; joystick.knobY = dy;
                if (col) col->setJoystickInput(dx / JOYSTICK_MAX_DIST, dy / JOYSTICK_MAX_DIST);
            }
            break;
        }
        case 1: case 3:
            joystick.active = false; joystick.knobX = 0; joystick.knobY = 0;
            if (col) col->setJoystickInput(0, 0); break;
    }
}

void GameUI::performRightClick() {
    auto* gameForInv = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!gameForInv) return;
    auto& inv = *gameForInv->getInventory();
    const InvSlot& held = inv.getHotbarSlot(inv.getSelectedSlot());
    bool hasItem = held.present && held.itemId > 0;

    // 手持食物且饥饿值未满 → 吃东西（发送 UseItem 包）
    if (hasItem) {
        std::string itemName = ClientEngine::getInstance()->getBlockRegistry()->getItemName(held.itemId);
        static const std::unordered_set<std::string> foodItems = {
            "apple", "bread", "cooked_beef", "cooked_porkchop", "cooked_chicken",
            "cooked_cod", "cooked_salmon", "beef", "porkchop", "chicken",
            "cod", "salmon", "potato", "baked_potato", "carrot",
            "golden_carrot", "golden_apple", "enchanted_golden_apple",
            "melon_slice", "sweet_berries", "glow_berries",
            "pumpkin_pie", "cookie", "mushroom_stew",
            "beetroot_soup", "rabbit_stew", "suspicious_stew",
            "dried_kelp", "beetroot", "poisonous_potato",
            "spider_eye", "rotten_flesh", "chorus_fruit",
            "cooked_mutton", "mutton", "cooked_rabbit", "rabbit",
            "honey_bottle"
        };
        if (foodItems.find(itemName) != foodItems.end()) {
            auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
            if (game) {
                game->sendUseItem(0);
                LOGI("Ate food: %s", itemName.c_str());
            }
            return;
        }
    }

    auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!engine) return;
    auto* raycast = engine->getRaycast();
    auto* cm = engine->getChunkManager();
    if (!raycast || !cm) return;
    auto result = raycast->rayCastFromCamera(4.5f);
    if (!result.hit) return;

    auto chunk = cm->getChunk(result.blockX >> 4, result.blockZ >> 4);
    if (!chunk) return;
    uint32_t state = chunk->getBlockState(result.blockX & 15, result.blockY, result.blockZ & 15);
    auto meta = ClientEngine::getInstance()->getBlockRegistry()->getBlockMetadata(state);

    // 检查目标是否为可交互方块（门、活板门、工作台、箱子等）
    // 门类方块的名称以 _door 或 _trapdoor 结尾，但铁门只能由红石控制
    bool isDoorLike = (meta.name.length() > 5 &&
        meta.name.compare(meta.name.length() - 5, 5, "_door") == 0 &&
        meta.name != "iron_door") ||
        (meta.name.length() > 9 &&
        meta.name.compare(meta.name.length() - 9, 9, "_trapdoor") == 0);

    bool isInteractive = isDoorLike ||
        meta.name == "crafting_table" || meta.name == "furnace" ||
        meta.name == "chest" || meta.name == "ender_chest" ||
        meta.name == "anvil" || meta.name == "enchanting_table" ||
        meta.name == "brewing_stand" || meta.name == "smithing_table";

    if (isInteractive) {
        // 可交互方块：发送 UseItemOn 打开/切换
        engine->sendBlockPlacement(result.blockX, result.blockY, result.blockZ, result.hitFace, 0);
    } else if (hasItem) {
        // 非交互方块 + 手持物品：在相邻面放置方块
        engine->sendBlockPlacement(result.blockX, result.blockY, result.blockZ, result.hitFace, 0);
    }
}

// ===== 挖掘逻辑（对标原版 MultiPlayerGameMode）=====

// 根据方块名称映射到音效类别（对应 sounds 目录下的 step/ dig/ 子目录）
static std::string getBlockSoundCategory(uint32_t blockState) {
    std::string name = ClientEngine::getInstance()->getBlockRegistry()->getBlockName(blockState);
    if (name.empty()) return "stone";

    // 按常见方块类型分组
    if (name.find("stone") != std::string::npos ||
        name.find("_ore") != std::string::npos ||
        name.find("deepslate") != std::string::npos ||
        name.find("andesite") != std::string::npos ||
        name.find("diorite") != std::string::npos ||
        name.find("granite") != std::string::npos ||
        name.find("tuff") != std::string::npos ||
        name.find("calcite") != std::string::npos ||
        name.find("basalt") != std::string::npos ||
        name.find("blackstone") != std::string::npos ||
        name.find("netherrack") != std::string::npos ||
        name.find("end_stone") != std::string::npos)
        return "stone";

    if (name.find("grass") != std::string::npos ||
        name.find("dirt") != std::string::npos ||
        name.find("podzol") != std::string::npos ||
        name.find("mycelium") != std::string::npos ||
        name.find("farmland") != std::string::npos ||
        name.find("path") != std::string::npos)
        return "grass";

    if (name.find("_planks") != std::string::npos ||
        name.find("_log") != std::string::npos ||
        name.find("_wood") != std::string::npos ||
        name.find("fence") != std::string::npos ||
        name.find("door") != std::string::npos ||
        name.find("trapdoor") != std::string::npos ||
        name.find("ladder") != std::string::npos)
        return "wood";

    if (name.find("sand") != std::string::npos)
        return "sand";
    if (name.find("gravel") != std::string::npos)
        return "gravel";
    if (name.find("_wool") != std::string::npos ||
        name.find("carpet") != std::string::npos)
        return "cloth";
    if (name.find("_glass") != std::string::npos)
        return "glass";
    if (name.find("snow") != std::string::npos)
        return "snow";
    if (name.find("_concrete") != std::string::npos)
        return "stone";
    if (name.find("terracotta") != std::string::npos ||
        name.find("clay") != std::string::npos)
        return "stone";
    if (name.find("cobblestone") != std::string::npos ||
        name.find("mossy_cobblestone") != std::string::npos)
        return "stone";
    if (name.find("gravel") != std::string::npos)
        return "gravel";
    if (name.find("nether_wart") != std::string::npos ||
        name.find("wart_block") != std::string::npos)
        return "stone";
    if (name.find("shroomlight") != std::string::npos)
        return "shroomlight";
    if (name.find("root") != std::string::npos ||
        name.find("vine") != std::string::npos)
        return "grass";
    if (name.find("nylium") != std::string::npos)
        return "nylium";
    if (name.find("crimson") != std::string::npos ||
        name.find("warped") != std::string::npos)
        return "stem";

    return "stone";
}

// 首次按下攻击按钮时调用
void GameUI::performLeftClick() {
    auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!engine) return;
    auto* raycast = engine->getRaycast();
    if (!raycast) return;

    // 综合检测：优先准星下的实体，其次方块
    auto target = raycast->getTargetFromCamera(4.5f, engine->getPlayerId());
    if (target.hasEntity()) {
        engine->sendEntityAttack(target.entityId);
        destroyDelay = 5; // 攻击后冷却（防止攻击过快）
        return;
    }

    int gameMode = engine->getGameMode();
    auto* cm = engine->getChunkManager();
    if (!cm) return;
    auto result = target.block;
    if (!result.hit) return;

    // 创造模式：瞬时破坏 + 5刻冷却
    if (gameMode == 1) {
        engine->sendBlockBreakStart(result.blockX, result.blockY, result.blockZ, result.hitFace);
        destroyDelay = 5;
        destroyAccumulator = 0.0f;
        digging = false;
        return;
    }

    // 生存/冒险模式：如果正在挖掘且目标变了，先 ABORT 旧的
    if (digging) {
        engine->sendBlockBreakAbort(digBlockX, digBlockY, digBlockZ, digFace);
    }

    // 开始新挖掘
    engine->sendBlockBreakStart(result.blockX, result.blockY, result.blockZ, result.hitFace);
    digBlockX = result.blockX; digBlockY = result.blockY; digBlockZ = result.blockZ;
    digFace = result.hitFace;
    destroyProgress = 0.0f;
    destroyAccumulator = 0.0f;

    // 查询方块硬度，瞬时破坏（hardness=0 或空气）
    if (cm) {
        auto chunk = cm->getChunk(result.blockX >> 4, result.blockZ >> 4);
        if (chunk) {
            uint32_t state = chunk->getBlockState(result.blockX & 15, result.blockY, result.blockZ & 15);
            auto meta = ClientEngine::getInstance()->getBlockRegistry()->getBlockMetadata(state);
            if (meta.hardness < 0.0f) {
                // 不可破坏（基岩等）
                digging = false;
                return;
            }
            if (meta.hardness == 0.0f) {
                // 瞬间破坏（火把、花等）
                // 播放破坏音效
                {
                    std::string cat = getBlockSoundCategory(state);
                    MusicManager::getInstance().playOneShot("dig/" + cat + "1");
                }
                engine->sendBlockBreakFinish(result.blockX, result.blockY, result.blockZ, result.hitFace);
                destroyDelay = 5;
                destroyAccumulator = 0.0f;
                digging = false;
                return;
            }
        }
    }

    digging = true;
}

// ===== 原版挖掘公式：playerDigSpeed / hardness / divisor =====

// 根据手持物品计算玩家挖掘速度（对标 ItemStack/DiggerItem.getDestroySpeed）
// 工具速度加成仅对匹配的方块类型生效
static float getPlayerDigSpeed(const std::string& material) {
    auto* g = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!g) return 1.0f;
    auto& inv = *g->getInventory();
    const InvSlot& held = inv.getHotbarSlot(inv.getSelectedSlot());
    if (!held.present || held.itemId <= 0) return 1.0f; // 空手

    std::string itemName = ClientEngine::getInstance()->getBlockRegistry()->getItemName(held.itemId);

    // 判断工具类型是否匹配该方块材质
    bool isPickaxe = itemName.find("pickaxe") != std::string::npos;
    bool isAxe = itemName.find("axe") != std::string::npos && !isPickaxe;
    bool isShovel = itemName.find("shovel") != std::string::npos;
    bool isHoe = itemName.find("hoe") != std::string::npos;

    // 提取方块材质需求（mineable/后面的部分）
    std::string requiredType;
    if (material.find("mineable/") == 0) {
        requiredType = material.substr(9);
    }

    bool matches = false;
    if (requiredType == "pickaxe") matches = isPickaxe;
    else if (requiredType == "axe") matches = isAxe;
    else if (requiredType == "shovel") matches = isShovel;
    else if (requiredType == "hoe") matches = isHoe;

    // 工具不匹配此方块时，返回空手速度 1.0
    if (!matches) return 1.0f;

    // 工具匹配 → 返回对应材质的工具速度
    if (itemName.find("wooden_") != std::string::npos) return 2.0f;
    if (itemName.find("stone_") != std::string::npos) return 4.0f;
    if (itemName.find("iron_") != std::string::npos) return 6.0f;
    if (itemName.find("diamond_") != std::string::npos) return 8.0f;
    if (itemName.find("netherite_") != std::string::npos) return 9.0f;
    if (itemName.find("golden_") != std::string::npos) return 12.0f;

    return 1.0f; // 非工具物品
}

// 检查手持物品是否为该材质对应的正确工具
static bool hasCorrectToolFor(const std::string& material, bool requiresCorrectTool) {
    if (!requiresCorrectTool) return true; // 不需要工具 → 总是有"正确工具"

    auto* g = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!g) return false;
    auto& inv = *g->getInventory();
    const InvSlot& held = inv.getHotbarSlot(inv.getSelectedSlot());
    if (!held.present || held.itemId <= 0) return false; // 空手

    std::string itemName = ClientEngine::getInstance()->getBlockRegistry()->getItemName(held.itemId);
    std::string requiredType = material.substr(9); // "pickaxe", "shovel" 等

    if (requiredType == "pickaxe") return itemName.find("pickaxe") != std::string::npos;
    if (requiredType == "shovel") return itemName.find("shovel") != std::string::npos;
    if (requiredType == "axe") return itemName.find("axe") != std::string::npos
                                    && itemName.find("pickaxe") == std::string::npos;
    if (requiredType == "hoe") return itemName.find("hoe") != std::string::npos;

    return false;
}

// 每帧持续调用（对标 continueDestroyBlock）
void GameUI::continueDestroyBlock() {
    auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!engine) return;
    auto* raycast = engine->getRaycast();
    if (!raycast) return;

    // 综合检测：每帧优先检测实体，其次方块
    auto target = raycast->getTargetFromCamera(4.5f, engine->getPlayerId());
    if (target.hasEntity()) {
        engine->sendEntityAttack(target.entityId);
        destroyDelay = 5;
        // 如果正在挖矿则中断
        if (digging) {
            engine->sendBlockBreakAbort(digBlockX, digBlockY, digBlockZ, digFace);
            digging = false;
            destroyProgress = 0.0f;
        }
        return;
    }

    auto* cm = engine->getChunkManager();
    if (!cm) return;

    auto result = target.block;

    // 未命中或目标切换：ABORT 旧 + START 新
    if (!result.hit ||
        result.blockX != digBlockX || result.blockY != digBlockY || result.blockZ != digBlockZ) {
        if (digging) {
            engine->sendBlockBreakAbort(digBlockX, digBlockY, digBlockZ, digFace);
            digging = false;
        }
        if (result.hit) {
            performLeftClick();
        }
        return;
    }

    // 未处于挖掘状态（可能刚被 ABORT）
    if (!digging) return;

    // 查询方块属性，计算每刻进度增量
    auto chunk = cm->getChunk(result.blockX >> 4, result.blockZ >> 4);
    if (!chunk) return;
    uint32_t state = chunk->getBlockState(result.blockX & 15, result.blockY, result.blockZ & 15);
    auto meta = ClientEngine::getInstance()->getBlockRegistry()->getBlockMetadata(state);

    if (meta.hardness < 0.0f) {
        engine->sendBlockBreakAbort(digBlockX, digBlockY, digBlockZ, digFace);
        digging = false;
        return;
    }

    // 原版公式：progressPerTick = playerDigSpeed / hardness / divisor
    // divisor = 30（正确工具或不需要工具）, 100（无正确工具）
    int divisor = hasCorrectToolFor(meta.material, meta.requiresCorrectTool) ? 30 : 100;
    float playerSpeed = getPlayerDigSpeed(meta.material);
    float progressPerTick = playerSpeed / meta.hardness / divisor;

    // 固定刻率（50ms/tick）累加进度
    destroyAccumulator += ImGui::GetIO().DeltaTime;
    while (destroyAccumulator >= 0.05f) {
        destroyAccumulator -= 0.05f;
        destroyProgress += progressPerTick;
    }

    // 挖掘完成
    if (destroyProgress >= 1.0f) {
        // 播放破坏音效
        {
            std::string cat = getBlockSoundCategory(state);
            MusicManager::getInstance().playOneShot("dig/" + cat + "1");
        }
        engine->sendBlockBreakFinish(digBlockX, digBlockY, digBlockZ, digFace);
        digging = false;
        destroyProgress = 0.0f;
        destroyDelay = 5;
        destroyAccumulator = 0.0f;
    }
}

// 松开攻击按钮时调用（对标 stopDestroyBlock）
void GameUI::stopDestroyBlock() {
    if (digging) {
        auto* engine = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
        if (engine) {
            engine->sendBlockBreakAbort(digBlockX, digBlockY, digBlockZ, digFace);
        }
        digging = false;
        destroyProgress = 0.0f;
        destroyAccumulator = 0.0f;
    }
}

void GameUI::handleCameraTouch(int pointerId, float x, float y, int action) {
    auto* pt = findTouchPoint(pointerId);
    if (!pt) return;
    switch (action) {
        case 0: pt->cameraLastX = x; pt->cameraLastY = y; break;
        case 2: {
            float dx = x - pt->cameraLastX, dy = y - pt->cameraLastY;
            CameraController::getInstance().updateRotation(dy * 0.005f, dx * 0.005f);
            pt->cameraLastX = x; pt->cameraLastY = y; break;
        }
    }
}

// ===== 触摸区域检测 =====

bool GameUI::isInJoystickArea(float x, float y) const {
    float jx = JOYSTICK_CENTER_X;
    float jy = ImGui::GetIO().DisplaySize.y - JOYSTICK_CENTER_Y_OFFSET;
    float dx = x - jx, dy = y - jy;
    return (dx * dx + dy * dy) <= (JOYSTICK_RADIUS * JOYSTICK_RADIUS * 2.25f);
}

bool GameUI::isInUpButtonArea(float x, float y) const {
    float bx = ImGui::GetIO().DisplaySize.x - BTN_RIGHT_MARGIN;
    float by = ImGui::GetIO().DisplaySize.y * 0.5f - BTN_VERTICAL_SPACING;
    return ((x-bx)*(x-bx) + (y-by)*(y-by)) <= (BTN_RADIUS * BTN_RADIUS * 1.5f);
}

bool GameUI::isInDownButtonArea(float x, float y) const {
    float bx = ImGui::GetIO().DisplaySize.x - BTN_RIGHT_MARGIN;
    float by = ImGui::GetIO().DisplaySize.y * 0.5f;
    return ((x-bx)*(x-bx) + (y-by)*(y-by)) <= (BTN_RADIUS * BTN_RADIUS * 1.5f);
}

bool GameUI::isInSprintButtonArea(float x, float y) const {
    float bx = ImGui::GetIO().DisplaySize.x - BTN_RIGHT_MARGIN;
    float by = ImGui::GetIO().DisplaySize.y * 0.5f + BTN_VERTICAL_SPACING;
    return ((x-bx)*(x-bx) + (y-by)*(y-by)) <= (BTN_RADIUS * BTN_RADIUS * 1.5f);
}

bool GameUI::isInAttackButtonArea(float x, float y) const {
    float bx = ImGui::GetIO().DisplaySize.x - BTN_RIGHT_MARGIN - ImGui::GetIO().DisplaySize.x * 0.125f;
    float by = ImGui::GetIO().DisplaySize.y * 0.5f - BTN_VERTICAL_SPACING * 2 + ImGui::GetIO().DisplaySize.y * 0.21f;
    return ((x-bx)*(x-bx) + (y-by)*(y-by)) <= (BTN_RADIUS * BTN_RADIUS * 1.5f);
}

bool GameUI::isInPlaceButtonArea(float x, float y) const {
    float bx = ImGui::GetIO().DisplaySize.x - BTN_RIGHT_MARGIN - 200.0f;
    float by = ImGui::GetIO().DisplaySize.y * 0.5f - BTN_VERTICAL_SPACING * 2 + 150.0f + BTN_VERTICAL_SPACING;
    return ((x-bx)*(x-bx) + (y-by)*(y-by)) <= (BTN_RADIUS * BTN_RADIUS * 1.5f);
}

bool GameUI::isInF3ButtonArea(float x, float y) const {
    float w = ImGui::GetIO().DisplaySize.x, h = ImGui::GetIO().DisplaySize.y;
    const float F3_W = w * 0.0325f, F3_H = h * 0.039f;
    const float F3_X = w * 0.25f - F3_W * 0.5f;
    const float F3_Y = h * 0.014f;
    return x >= F3_X && x <= F3_X + F3_W && y >= F3_Y && y <= F3_Y + F3_H;
}

bool GameUI::isInChatButtonArea(float x, float y) const {
    float w = ImGui::GetIO().DisplaySize.x, h = ImGui::GetIO().DisplaySize.y;
    const float T_W = w * 0.0325f, T_H = h * 0.039f;
    const float T_X = w * 0.5f - T_W * 0.5f;
    const float T_Y = h * 0.014f;
    return x >= T_X && x <= T_X + T_W && y >= T_Y && y <= T_Y + T_H;
}

bool GameUI::isInEButtonArea(float x, float y) const {
    float w = ImGui::GetIO().DisplaySize.x, h = ImGui::GetIO().DisplaySize.y;
    const float SLOT_SIZE = w * 0.034f, SLOT_GAP = w * 0.003f, HOTBAR_Y = h * 0.915f;
    float totalW = 9.0f * SLOT_SIZE + 8.0f * SLOT_GAP;
    float hotbarX = w * 0.5f - totalW * 0.5f;
    float btnX = hotbarX + totalW + 10.0f;
    return x >= btnX && x <= btnX + SLOT_SIZE && y >= HOTBAR_Y && y <= HOTBAR_Y + SLOT_SIZE;
}

int GameUI::hotbarSlotAt(float x, float y) const {
    float h = ImGui::GetIO().DisplaySize.y, w = ImGui::GetIO().DisplaySize.x;
    const float SLOT_SIZE = w * 0.034f, SLOT_GAP = w * 0.003f, HOTBAR_Y = h * 0.915f;
    float totalW = 9.0f * SLOT_SIZE + 8.0f * SLOT_GAP;
    float hotbarX = w * 0.5f - totalW * 0.5f;
    if (y < HOTBAR_Y || y > HOTBAR_Y + SLOT_SIZE) return -1;
    float relX = x - hotbarX;
    float slotStep = SLOT_SIZE + SLOT_GAP;
    int slot = (int)(relX / slotStep);
    if (slot < 0 || slot > 8) return -1;
    float slotX = hotbarX + slot * slotStep;
    if (x < slotX || x > slotX + SLOT_SIZE) return -1;
    return slot;
}

// ===== 聊天系统（转发到 ChatScreen）=====

void GameUI::addChatMessage(const std::string& msg, unsigned int color) {
    if (m_chat) m_chat->addMessage(msg, color);
}

void GameUI::clearChatMessages() {
    if (m_chat) m_chat->clear();
}

void GameUI::openChat() {
    if (m_chat) m_chat->toggle();
}

bool GameUI::isChatOpen() const {
    return m_chat && m_chat->isOpen();
}

