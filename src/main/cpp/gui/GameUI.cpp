#include "GameUI.h"
#include <android/log.h>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>

#define IMGUI_IMPL_OPENGL_ES3
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <GLES3/gl3.h>
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "CameraController.h"
#include "Collision.h"
#include "PlayerInventory.h"
#include "ClientEngine.h"
#include "Raycast.h"
#include "ChunkManager.h"
#include "BlockRegistry.h"
#include "TextureLoader.h"

// Screen 系统（同目录）
#include "ScreenManager.h"
#include "TitleScreen.h"
#include "MultiplayerScreen.h"
#include "ConnectingScreen.h"
#include "HudScreen.h"
#include "DeathScreen.h"
#include "InventoryScreen.h"
#include "PauseScreen.h"

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

#define JOYSTICK_CENTER_X  220.0f
#define JOYSTICK_CENTER_Y_OFFSET 210.0f
#define JOYSTICK_RADIUS    110.0f
#define JOYSTICK_KNOB_RADIUS 38.0f
#define JOYSTICK_MAX_DIST  65.0f
#define BTN_RADIUS         38.0f
#define BTN_RIGHT_MARGIN   90.0f
#define BTN_VERTICAL_SPACING 85.0f

GameUI& GameUI::getInstance() {
    static GameUI instance;
    return instance;
}

bool GameUI::init() {
    if (initialized) return true;
    LOGI("Initializing ImGui...");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    static const char* fontPaths[] = {
        "/system/fonts/NotoSansSC-Regular.otf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
    };
    ImFont* font = nullptr;
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
        auto* engine = ClientEngine::getInstance();

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
        float health = engine ? engine->getHealth() : 20.0f;
        if (!deathScreenActive && health <= 0.0f && prevHealth > 0.0f && engine && engine->getGameMode() != 3) {
            deathScreenActive = true;
            std::string serverMsg = engine->getDeathMessage();
            deathReason = serverMsg.empty() ? "你被杀死了" : serverMsg;
        }
        prevHealth = health;
    }

    updateOverlays();
    ScreenManager::getInstance().render(0, 0);

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
    if (currentState == UIState::IN_GAME && (gameMenuOpen || optionsOpen || deathScreenActive)) {
        queueTouchEvent(x, y, action);
        return;
    }
    if (currentState == UIState::IN_GAME && inventoryOpen) {
        if (isInEButtonArea(x, y) && action == 0) {
            // 关闭外部容器时发送关闭包
            if (openContainerId > 0) {
                auto* engine = ClientEngine::getInstance();
                if (engine) engine->sendContainerClose();
            }
            closeContainer();
            Collision::getInstance().resetMovement();
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
            Collision::getInstance().setKeyState(GAMEKEY_UP, true);
            buttons.upPressed = true;
        } else if (!isRoleTaken(TouchPoint::DOWN_BUTTON) && isInDownButtonArea(x, y)) {
            pt->role = TouchPoint::DOWN_BUTTON;
            Collision::getInstance().setKeyState(GAMEKEY_DOWN, true);
            buttons.downPressed = true;
        } else if (!isRoleTaken(TouchPoint::SPRINT_BUTTON) && isInSprintButtonArea(x, y)) {
            pt->role = TouchPoint::SPRINT_BUTTON;
            Collision::getInstance().setKeyState(GAMEKEY_SPRINT, true);
            buttons.sprintPressed = !buttons.sprintPressed;
        } else if (!isRoleTaken(TouchPoint::ATTACK_BUTTON) && isInAttackButtonArea(x, y)) {
            pt->role = TouchPoint::ATTACK_BUTTON;
            buttons.attackPressed = true;
            performBlockBreak();
        } else if (!isRoleTaken(TouchPoint::PLACE_BUTTON) && isInPlaceButtonArea(x, y)) {
            pt->role = TouchPoint::PLACE_BUTTON;
            buttons.placePressed = true;
            performBlockPlacement();
        } else if (!isRoleTaken(TouchPoint::F3_BUTTON) && isInF3ButtonArea(x, y)) {
            pt->role = TouchPoint::F3_BUTTON;
            toggleDebugInfo();
        } else if (!isRoleTaken(TouchPoint::E_BUTTON) && isInEButtonArea(x, y)) {
            pt->role = TouchPoint::E_BUTTON;
            if (inventoryOpen) {
                // 关闭外部容器时发送关闭包
                if (openContainerId > 0) {
                    auto* engine = ClientEngine::getInstance();
                    if (engine) engine->sendContainerClose();
                }
                closeContainer();
            } else {
                inventoryOpen = true;
            }
            Collision::getInstance().resetMovement();
        } else if (currentState == UIState::IN_GAME) {
            int hbSlot = hotbarSlotAt(x, y);
            if (hbSlot >= 0) {
                PlayerInventory::getInstance().setSelectedSlot(hbSlot);
                ClientEngine::getInstance()->sendHeldItemChange(hbSlot);
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
            case TouchPoint::UP_BUTTON:
                Collision::getInstance().setKeyState(GAMEKEY_UP, false);
                buttons.upPressed = false; break;
            case TouchPoint::DOWN_BUTTON:
                Collision::getInstance().setKeyState(GAMEKEY_DOWN, false);
                buttons.downPressed = false; break;
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
    switch (action) {
        case 0: {
            joystick.active = true;
            float dx = x - jx, dy = y - jy;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > JOYSTICK_MAX_DIST) { dx = dx / dist * JOYSTICK_MAX_DIST; dy = dy / dist * JOYSTICK_MAX_DIST; }
            joystick.knobX = dx; joystick.knobY = dy;
            Collision::getInstance().setJoystickInput(dx / JOYSTICK_MAX_DIST, dy / JOYSTICK_MAX_DIST);
            break;
        }
        case 2: {
            if (joystick.active) {
                float dx = x - jx, dy = y - jy;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist > JOYSTICK_MAX_DIST) { dx = dx / dist * JOYSTICK_MAX_DIST; dy = dy / dist * JOYSTICK_MAX_DIST; }
                joystick.knobX = dx; joystick.knobY = dy;
                Collision::getInstance().setJoystickInput(dx / JOYSTICK_MAX_DIST, dy / JOYSTICK_MAX_DIST);
            }
            break;
        }
        case 1: case 3:
            joystick.active = false; joystick.knobX = 0; joystick.knobY = 0;
            Collision::getInstance().setJoystickInput(0, 0); break;
    }
}

void GameUI::performBlockPlacement() {
    auto& inv = PlayerInventory::getInstance();
    const InvSlot& held = inv.getHotbarSlot(inv.getSelectedSlot());
    bool hasItem = held.present && held.itemId > 0;

    auto& cam = CameraController::getInstance();
    glm::vec3 playerPos = cam.getPosition();
    float pitch = cam.getPitch(), yaw = cam.getYaw();
    glm::vec3 dir;
    dir.x = -std::sin(yaw) * std::cos(pitch);
    dir.y = -std::sin(pitch);
    dir.z = std::cos(yaw) * std::cos(pitch);
    glm::vec3 eyePos = playerPos + glm::vec3(0.0f, 1.62f, 0.0f);
    auto* engine = ClientEngine::getInstance();
    if (!engine) return;
    auto* cm = engine->getChunkManager();
    if (!cm) return;
    auto result = rayCast(eyePos, dir, 5.0f, *cm);
    if (!result.hit) return;

    // 空手时：检查目标方块是否为可交互方块（工作台等）
    if (!hasItem) {
        auto chunk = cm->getChunk(result.blockX >> 4, result.blockZ >> 4);
        if (!chunk) return;
        uint32_t state = chunk->getBlockState(result.blockX & 15, result.blockY, result.blockZ & 15);
        auto meta = BlockRegistry::getInstance().getBlockMetadata(state);
        // 只对可交互方块发送 UseItemOn（工作台、熔炉、箱子等）
        if (meta.name != "crafting_table" && meta.name != "furnace" &&
            meta.name != "chest" && meta.name != "ender_chest" &&
            meta.name != "anvil" && meta.name != "enchanting_table" &&
            meta.name != "brewing_stand" && meta.name != "smithing_table") {
            return;
        }
    }

    static const glm::ivec3 faceNormals[] = {
        {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}
    };
    engine->sendBlockPlacement(result.blockX, result.blockY, result.blockZ, result.hitFace, 0);
}

// ===== 挖掘逻辑（对标原版 MultiPlayerGameMode）=====

RaycastResult GameUI::getTargetBlock() const {
    auto& cam = CameraController::getInstance();
    glm::vec3 playerPos = cam.getPosition();
    float pitch = cam.getPitch(), yaw = cam.getYaw();
    glm::vec3 dir;
    dir.x = -std::sin(yaw) * std::cos(pitch);
    dir.y = -std::sin(pitch);
    dir.z = std::cos(yaw) * std::cos(pitch);
    glm::vec3 eyePos = playerPos + glm::vec3(0.0f, 1.62f, 0.0f);
    auto* engine = ClientEngine::getInstance();
    if (!engine) return {};
    auto* cm = engine->getChunkManager();
    if (!cm) return {};
    return rayCast(eyePos, dir, 5.0f, *cm);
}

// 首次按下攻击按钮时调用
void GameUI::performBlockBreak() {
    auto* engine = ClientEngine::getInstance();
    if (!engine) return;
    int gameMode = engine->getGameMode();
    auto result = getTargetBlock();
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
    auto* cm = engine->getChunkManager();
    if (cm) {
        auto chunk = cm->getChunk(result.blockX >> 4, result.blockZ >> 4);
        if (chunk) {
            uint32_t state = chunk->getBlockState(result.blockX & 15, result.blockY, result.blockZ & 15);
            auto meta = BlockRegistry::getInstance().getBlockMetadata(state);
            if (meta.hardness < 0.0f) {
                // 不可破坏（基岩等）
                digging = false;
                return;
            }
            if (meta.hardness == 0.0f) {
                // 瞬间破坏（火把、花等）
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
    auto& inv = PlayerInventory::getInstance();
    const InvSlot& held = inv.getHotbarSlot(inv.getSelectedSlot());
    if (!held.present || held.itemId <= 0) return 1.0f; // 空手

    std::string itemName = BlockRegistry::getInstance().getItemName(held.itemId);

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

    auto& inv = PlayerInventory::getInstance();
    const InvSlot& held = inv.getHotbarSlot(inv.getSelectedSlot());
    if (!held.present || held.itemId <= 0) return false; // 空手

    std::string itemName = BlockRegistry::getInstance().getItemName(held.itemId);
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
    auto* engine = ClientEngine::getInstance();
    if (!engine) return;
    auto* cm = engine->getChunkManager();
    if (!cm) return;

    auto result = getTargetBlock();

    // 未命中或目标切换：ABORT 旧 + START 新
    if (!result.hit ||
        result.blockX != digBlockX || result.blockY != digBlockY || result.blockZ != digBlockZ) {
        if (digging) {
            engine->sendBlockBreakAbort(digBlockX, digBlockY, digBlockZ, digFace);
            digging = false;
        }
        if (result.hit) {
            performBlockBreak();
        }
        return;
    }

    // 未处于挖掘状态（可能刚被 ABORT）
    if (!digging) return;

    // 查询方块属性，计算每刻进度增量
    auto chunk = cm->getChunk(result.blockX >> 4, result.blockZ >> 4);
    if (!chunk) return;
    uint32_t state = chunk->getBlockState(result.blockX & 15, result.blockY, result.blockZ & 15);
    auto meta = BlockRegistry::getInstance().getBlockMetadata(state);

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
        auto* engine = ClientEngine::getInstance();
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
    float bx = ImGui::GetIO().DisplaySize.x - BTN_RIGHT_MARGIN - 200.0f;
    float by = ImGui::GetIO().DisplaySize.y * 0.5f - BTN_VERTICAL_SPACING * 2 + 150.0f;
    return ((x-bx)*(x-bx) + (y-by)*(y-by)) <= (BTN_RADIUS * BTN_RADIUS * 1.5f);
}

bool GameUI::isInPlaceButtonArea(float x, float y) const {
    float bx = ImGui::GetIO().DisplaySize.x - BTN_RIGHT_MARGIN - 200.0f;
    float by = ImGui::GetIO().DisplaySize.y * 0.5f - BTN_VERTICAL_SPACING * 2 + 150.0f + BTN_VERTICAL_SPACING;
    return ((x-bx)*(x-bx) + (y-by)*(y-by)) <= (BTN_RADIUS * BTN_RADIUS * 1.5f);
}

bool GameUI::isInF3ButtonArea(float x, float y) const {
    const float F3_W = 52.0f, F3_H = 28.0f;
    const float F3_X = ImGui::GetIO().DisplaySize.x * 0.25f - F3_W * 0.5f;
    const float F3_Y = 10.0f;
    return x >= F3_X && x <= F3_X + F3_W && y >= F3_Y && y <= F3_Y + F3_H;
}

bool GameUI::isInEButtonArea(float x, float y) const {
    float w = ImGui::GetIO().DisplaySize.x, h = ImGui::GetIO().DisplaySize.y;
    const float SLOT_SIZE = 55.0f, SLOT_GAP = 5.0f, HOTBAR_Y = h - 61.0f;
    float totalW = 9.0f * SLOT_SIZE + 8.0f * SLOT_GAP;
    float hotbarX = w * 0.5f - totalW * 0.5f;
    float btnX = hotbarX + totalW + 10.0f;
    return x >= btnX && x <= btnX + SLOT_SIZE && y >= HOTBAR_Y && y <= HOTBAR_Y + SLOT_SIZE;
}

int GameUI::hotbarSlotAt(float x, float y) const {
    float h = ImGui::GetIO().DisplaySize.y, w = ImGui::GetIO().DisplaySize.x;
    const float SLOT_SIZE = 55.0f, SLOT_GAP = 5.0f, HOTBAR_Y = h - 61.0f;
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
