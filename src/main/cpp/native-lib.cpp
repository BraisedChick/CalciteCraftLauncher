#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <thread>
#include <atomic>
#include "ClientEngine/ClientEngine.h"
#include "ClientEngine/GameEngine.h"
#include "VulkanRenderer.h"
#include "GLRenderer.h"
#include "utils.h"
#include "TextureLoader.h"
#include "ResourcepackManager.h"
#include "BlockRegistry.h"
#include "CameraController.h"
#include "Collision.h"
#include "Light.h"
#include "gui/GameUI.h"
#include "MusicManager.h"
#include "imgui.h"
#include "gui/ScreenManager.h"
#include "gui/TitleScreen.h"
#include "gui/ConnectingScreen.h"
#include "JniBridge.h"

#define JNI_LOG_TAG "JNI"
#define JNI_LOGI(...) __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG, __VA_ARGS__)
#define JNI_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG, __VA_ARGS__)

static VulkanRenderer* g_vulkanRenderer = nullptr;
static std::unique_ptr<GLRenderer> g_pendingRenderer;  // 引擎未创建时暂存渲染器，断开连接后也暂存
static bool g_useVulkan = false;
static bool g_initialized = false;
static bool g_rendererTypeSet = false;  // 标记是否已设置渲染器类型
static std::atomic<bool> g_rendering(false);
static std::atomic<bool> g_renderThreadIdle(false);  // 渲染线程已进入非游戏分支（安全删除引擎）

static std::thread g_renderThread;

static void renderLoop() {
    JNI_LOGI("Render thread started");

    // 在渲染线程中重新绑定 EGL context
    auto* renderLoopRenderer = ClientEngine::getInstance() ? ClientEngine::getInstance()->getRenderer() : g_pendingRenderer.get();
    if (renderLoopRenderer) {
        renderLoopRenderer->makeCurrent();
    }

    int frameCount = 0;
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (g_rendering) {
        frameCount++;

        // 计算 delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        bool inGame = (GameUI::getInstance().getState() == UIState::IN_GAME);

        if (inGame) {
            g_renderThreadIdle.store(false, std::memory_order_release);

            // 确保 PlayerController 和 Light 持有 ChunkManager 引用
            auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
            if (game) {
                auto* cm = game->getChunkManager();
                if (cm) {
                    if (!Collision::getInstance().hasChunkManager()) {
                        Collision::getInstance().setChunkManager(cm);
                        JNI_LOGI("PlayerController: ChunkManager acquired from engine");
                    }
                    Light::getInstance().setChunkManager(cm);
                }
            }

            // 背包/菜单/死亡界面打开时，重置移动输入但不中断物理（玩家仍受重力下落）
            if (game && GameUI::getInstance().isInGameUIActive()) {
                Collision::getInstance().resetMovement();
            }

            // 更新玩家物理（传入视角方向计算移动）
            float camPitch = CameraController::getInstance().getPitch();
            float camYaw = CameraController::getInstance().getYaw();
            glm::vec3 physPos;
            bool onGround = false;
            Collision::getInstance().update(deltaTime, camPitch, camYaw, &physPos, &onGround);

            // 发送玩家移动数据包到服务器（使用精确物理位置，已原子读取，无竞态）
            if (game) {
                float curPitch = CameraController::getInstance().getPitch();
                float curYaw = CameraController::getInstance().getYaw();
                game->sendPlayerMovement(physPos.x, physPos.y, physPos.z, curYaw, curPitch, onGround);
            }

            if (frameCount <= 5 || frameCount % 60 == 0) {
                JNI_LOGI("Rendering frame %d", frameCount);
            }

            // 从 CameraController 获取摄像机数据
            auto pos = CameraController::getInstance().getSmoothPosition();
            float pitch = CameraController::getInstance().getPitch();
            float yaw = CameraController::getInstance().getYaw();

            if (ClientEngine::getInstance() && ClientEngine::getInstance()->getRenderer()) {
                auto* renderer = ClientEngine::getInstance()->getRenderer();
                // 检查是否有已完成的光照重算，标记邻近 chunk
                {
                    int lx, ly, lz;
                    if (Light::getInstance().pollCompletedLightRecalc(&lx, &ly, &lz)) {
                        int cx = lx >> 4;
                        int cz = lz >> 4;
                        for (int dx = -2; dx <= 2; dx++) {
                            for (int dz = -2; dz <= 2; dz++) {
                                renderer->markChunkForUpdate(cx + dx, cz + dz);
                            }
                        }
                    }
                }

                renderer->render(pos.x, pos.y, pos.z, pitch, yaw);
            } else if (!ClientEngine::getInstance() && g_pendingRenderer) {
                g_pendingRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
            } else if (g_useVulkan && g_vulkanRenderer) {
                g_vulkanRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
            }
        } else {
            // 非游戏状态：不访问任何引擎资源，GLRenderer::render() 内部渲染全景+ImGui
            g_renderThreadIdle.store(true, std::memory_order_release);

            auto pos = CameraController::getInstance().getSmoothPosition();
            float pitch = CameraController::getInstance().getPitch();
            float yaw = CameraController::getInstance().getYaw();

            auto* activeRenderer = ClientEngine::getInstance() ? ClientEngine::getInstance()->getRenderer() : g_pendingRenderer.get();
            if (activeRenderer) {
                activeRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
            } else if (g_useVulkan && g_vulkanRenderer) {
                g_vulkanRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
            }
        }

        // 每帧检查 ImGui 是否需要键盘输入（所有渲染路径通用）
        {
            static bool lastWantTextInput = false;
            bool wantText = GameUI::getInstance().wantsTextInput();
            
            if (wantText != lastWantTextInput) {
                lastWantTextInput = wantText;
                
                if (JniBridge::getJvm() && JniBridge::getActivity()) {
                    JNIEnv* env;
                    bool attached = false;
                    int ger = JniBridge::getJvm()->GetEnv((void**)&env, JNI_VERSION_1_6);
                    if (ger == JNI_EDETACHED) {
                        if (JniBridge::getJvm()->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true;
                    } else if (ger == JNI_OK) {
                        // already attached
                    } else {
                        env = nullptr;
                    }
                    if (env) {
                        jclass clazz = env->GetObjectClass(JniBridge::getActivity());
                        jmethodID method = env->GetMethodID(clazz, "showKeyboardImGui", "(Z)V");
                        if (method) {
                            env->CallVoidMethod(JniBridge::getActivity(), method, wantText);
                        }
                        env->DeleteLocalRef(clazz);
                        if (attached) JniBridge::detachCurrentThread();
                    }
                }
            }
        }

        // 音乐管理器每帧驱动
        {
            auto& music = MusicManager::getInstance();
            if (music.isInitialized()) {
                // 根据 UI 状态同步音乐场景
                UIState uiState = GameUI::getInstance().getState();
                MusicScene targetScene;
                if (uiState == UIState::IN_GAME) {
                    targetScene = MusicScene::GAME; // TODO: 创造模式判断
                } else {
                    targetScene = MusicScene::MENU;
                }
                if (music.getScene() != targetScene) {
                    music.setScene(targetScene);
                }
                music.tick();
            }
        }
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_initRenderer(
        JNIEnv* env, jobject thiz, jobject surface) {

    JNI_LOGI("=== initRenderer called ===");

    // 保存 JavaVM 和 Activity 引用（用于回调控制 UI）
    JavaVM* jvm;
    env->GetJavaVM(&jvm);
    JniBridge::init(jvm, env->NewGlobalRef(thiz));

    // 加载 BlockRegistry（全局只加载一次，必须在渲染器初始化之前）
    static bool blockRegistryLoaded = false;
    if (!blockRegistryLoaded) {
        std::string blocksJsonContent = TextureLoader::readTextFromZip("blocks.json");
        if (!blocksJsonContent.empty() && BlockRegistry::getInstance().loadFromJson(blocksJsonContent)) {
            JNI_LOGI("BlockRegistry loaded successfully: %zu blocks",
                     BlockRegistry::getInstance().getBlockCount());
        } else {
            JNI_LOGE("Failed to load BlockRegistry, using fallback mapping");
        }

        std::string itemsJsonContent = TextureLoader::readTextFromZip("items.json");
        if (!itemsJsonContent.empty() && BlockRegistry::getInstance().loadItems(itemsJsonContent)) {
            JNI_LOGI("Items loaded successfully from ZIP");
        } else {
            JNI_LOGI("No items.json in ZIP, blocks-only mode");
        }

        blockRegistryLoaded = true;
    }

    if (!surface) {
        JNI_LOGE("Surface is null!");
        return;
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        JNI_LOGE("Failed to get ANativeWindow from surface");
        return;
    }

    int32_t width = ANativeWindow_getWidth(window);
    int32_t height = ANativeWindow_getHeight(window);

    JNI_LOGI("Window size: %dx%d", width, height);

    // 如果没有设置渲染器类型，默认使用 OpenGL ES
    if (!g_rendererTypeSet) {
        g_useVulkan = false;
        JNI_LOGI("No renderer type set, defaulting to OpenGL ES");
    }

    if (g_useVulkan) {
        // 使用 Vulkan
        JNI_LOGI("Initializing Vulkan renderer...");
        g_vulkanRenderer = new VulkanRenderer();

        if (!g_vulkanRenderer->initialize(window, width, height)) {
            JNI_LOGE("Failed to initialize Vulkan renderer");
            delete g_vulkanRenderer;
            g_vulkanRenderer = nullptr;
            ANativeWindow_release(window);
            return;
        }

        JNI_LOGI("Vulkan renderer initialized successfully");
    } else {
        // 使用 OpenGL ES
        JNI_LOGI("Initializing OpenGL ES renderer...");
        g_pendingRenderer = std::make_unique<GLRenderer>();

        if (!g_pendingRenderer->initialize(window)) {
            JNI_LOGE("Failed to initialize OpenGL renderer");
            g_pendingRenderer.reset();
            ANativeWindow_release(window);
            return;
        }

        JNI_LOGI("OpenGL ES renderer initialized successfully");
    }

    // 设置 ImGui 连接回调（在任意渲染器初始化后）
    {
        auto* activeRenderer = ClientEngine::getInstance() ? ClientEngine::getInstance()->getRenderer() : g_pendingRenderer.get();
        if (activeRenderer) {
        GameUI::getInstance().setConnectCallback([](const std::string& ip, int port) {
            JNI_LOGI("UI Connect: %s:%d user=%s", ip.c_str(), port, ClientEngine::getUsername().c_str());
            std::thread([ip, port]() {
                // 确保 ClientEngine 全局单例存在
                auto* client = ClientEngine::getInstance();
                if (!client) {
                    client = new ClientEngine();
                    // 将暂存的渲染器交给全局引擎
                    if (g_pendingRenderer) {
                        client->setRenderer(std::move(g_pendingRenderer));
                    }
                }

                // 销毁旧会话（如果有）
                client->destroyGame();

                // 创建新会话
                auto* game = client->createGame();

                // 传递正版认证信息给会话引擎
                if (ClientEngine::isPremiumPending()) {
                    game->setAuthInfo(
                        ClientEngine::getPendingAccessToken(),
                        ClientEngine::getPendingPlayerUuid(),
                        ClientEngine::getPendingTokenType());
                }

                // 加载语言文件
                {
                    std::string langJson = TextureLoader::readTextFromZip("lang/zh_cn.json");
                    if (!langJson.empty()) {
                        game->loadLanguage(langJson);
                    }
                }

                // 应用已加载的设置到渲染器
                if (client->getRenderer()) {
                    auto& ui = GameUI::getInstance();
                    client->getRenderer()->setFov(ui.getOptionsFov());
                    client->getRenderer()->setRenderDistance(ui.getRenderDistance());
                    client->getRenderer()->setMipmapLevel(ui.getMipmapLevel());
                    client->getRenderer()->setMaxFps(ui.getMaxFps());
                }

                // start() 会阻塞直到断开连接，所以先切换到 IN_GAME
                GameUI::getInstance().setState(UIState::IN_GAME);
                GameUI::getInstance().clearChatMessages();

                game->start(ip, port);

                // 断开连接后回到主菜单
                JNI_LOGI("Disconnected, returning to title screen");

                // 1. 先切状态 → 渲染线程下一帧自动进入安全分支（不访问引擎）
                auto& ui = GameUI::getInstance();
                ui.setState(UIState::MAIN_MENU);

                // 2. 等渲染线程确认已进入非游戏分支
                while (!g_renderThreadIdle.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                JNI_LOGI("Render thread in safe branch, cleaning up game engine");

                // 3. 清除单例中指向会话内部对象的裸指针
                Collision::getInstance().setChunkManager(nullptr);
                Light::getInstance().setChunkManager(nullptr);

                if (client->getRenderer()) {
                    client->getRenderer()->clearChunks();
                }

                // 4. 销毁会话引擎（ClientEngine 保持存活）
                client->destroyGame();
                JNI_LOGI("Game engine destroyed safely");

                // 5. 更新 UI
                ui.setGameMenuOpen(false);
                ui.setDeathScreenActive(false);
                ui.setOptionsOpen(false);
                ui.setInventoryOpen(false);
                ScreenManager::getInstance().setScreen(std::make_unique<TitleScreen>());
            }).detach();
        });
        }
    }

    g_initialized = true;
    ANativeWindow_release(window);

    // 初始化音乐管理器
    MusicManager::getInstance().init();

    JNI_LOGI("Starting render thread");
    g_rendering = true;
    g_renderThread = std::thread(renderLoop);

    // 等待一小段时间确保线程启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    JNI_LOGI("Render thread started");
    JNI_LOGI("=== initRenderer completed ===");
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_renderFrame(
        JNIEnv* env, jobject thiz) {

    if (!g_initialized || g_rendering) {
        // C++ 渲染线程已在运行，避免双重渲染冲突
        return;
    }

    // 从 CameraController 获取摄像机数据
    auto pos = CameraController::getInstance().getSmoothPosition();
    float pitch = CameraController::getInstance().getPitch();
    float yaw = CameraController::getInstance().getYaw();

    if (g_useVulkan && g_vulkanRenderer) {
        g_vulkanRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
    } else if (ClientEngine::getInstance() && ClientEngine::getInstance()->getRenderer()) {
        ClientEngine::getInstance()->getRenderer()->render(pos.x, pos.y, pos.z, pitch, yaw);
    } else if (g_pendingRenderer) {
        g_pendingRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_cleanupRenderer(
        JNIEnv* env,
        jobject thiz) {

    JNI_LOGI("=== cleanupRenderer called ===");

    // 1. 先停渲染线程（确保 EGL context 不再被使用）
    if (g_rendering) {
        g_rendering = false;
        if (g_renderThread.joinable()) {
            JNI_LOGI("Waiting for render thread to finish");
            g_renderThread.join();
            JNI_LOGI("Render thread joined");
        }
    }

    // 2. 使 EGL context 在当前线程活跃，才能安全删除 GL 资源
    GLRenderer* glRenderer = nullptr;
    if (ClientEngine::getInstance()) {
        glRenderer = ClientEngine::getInstance()->getRenderer();
    } else if (g_pendingRenderer) {
        glRenderer = g_pendingRenderer.get();
    }
    if (glRenderer) {
        glRenderer->makeCurrent();
    }

    // 3. 清除 GL 纹理缓存（现在 context 是当前的，glDeleteTextures 有效）
    ResourcepackManager::getInstance().clear();

    // 4. 删除渲染器（内部 cleanup 会销毁 EGL context）
    if (g_vulkanRenderer) {
        delete g_vulkanRenderer;
        g_vulkanRenderer = nullptr;
    }

    // 从引擎提取渲染器再删除引擎，或者直接删除暂存的渲染器
    if (ClientEngine::getInstance()) {
        g_pendingRenderer = ClientEngine::getInstance()->releaseRenderer();
        delete ClientEngine::getInstance();
    }
    g_pendingRenderer.reset();
    // 释放 Activity 全局引用，防止内存泄漏
    JniBridge::cleanup(env);

    g_initialized = false;

    // 关闭音乐管理器
    MusicManager::getInstance().shutdown();

    JNI_LOGI("Cleanup completed");
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_updateCameraAngle(
        JNIEnv* env, jobject thiz,
        jfloat pitchDelta, jfloat yawDelta) {

    // 更新 CameraController 的旋转（相对变化量）
    CameraController::getInstance().updateRotation(pitchDelta, yawDelta);
}

// ===== 新的输入控制接口 =====

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_setKeyState(
        JNIEnv* env, jobject thiz,
        jint key, jboolean pressed) {

    Collision::getInstance().setKeyState((int)key, (bool)pressed);
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_setCameraPosition(
        JNIEnv* env, jobject thiz,
        jfloat x, jfloat y, jfloat z,
        jfloat pitch, jfloat yaw) {

    // 设置初始位置（只在初始化时调用一次）
    CameraController::getInstance().setPosition(x, y, z);
    CameraController::getInstance().setRotation(pitch, yaw);
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_setAssetManager(
        JNIEnv* env,
        jobject thiz,
        jobject assetManager) {

    JNI_LOGI("Setting asset manager");

    AAssetManager* assets = AAssetManager_fromJava(env, assetManager);
    if (assets) {
        VulkanRenderer::setAssetManager(assets);
        TextureLoader::setAssetManager(assets);
        JNI_LOGI("Asset manager set for OpenGL ES, Vulkan renderers and TextureLoader");
    } else {
        JNI_LOGE("Failed to get asset manager");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_setTextureZipPath(
        JNIEnv* env,
        jobject thiz,
        jstring zipPath) {

    const char* path = env->GetStringUTFChars(zipPath, nullptr);
    TextureLoader::setZipPath(path);
    JNI_LOGI("Texture ZIP path set to: %s", path);
    env->ReleaseStringUTFChars(zipPath, path);
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_resizeRenderer(
        JNIEnv* env,
        jobject thiz,
        jint width,
        jint height) {

    JNI_LOGI("Resizing renderer to %dx%d", width, height);

    GLRenderer* glRenderer = nullptr;
    if (ClientEngine::getInstance()) {
        glRenderer = ClientEngine::getInstance()->getRenderer();
    } else if (g_pendingRenderer) {
        glRenderer = g_pendingRenderer.get();
    }

    if (glRenderer) {
        glRenderer->recreateSurface(width, height);
    } else if (g_vulkanRenderer) {
        g_vulkanRenderer->recreateSwapchain(width, height);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_setRendererType(
        JNIEnv* env,
        jobject thiz,
        jboolean useVulkan) {

    g_useVulkan = (bool)useVulkan;
    g_rendererTypeSet = true;
    JNI_LOGI("Renderer type set to: %s", useVulkan ? "Vulkan" : "OpenGL ES");
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_setUsername(
        JNIEnv* env,
        jobject thiz,
        jstring username) {

    const char* name = env->GetStringUTFChars(username, nullptr);
    ClientEngine::setUsername(name);
    JNI_LOGI("Username set: %s", ClientEngine::getUsername().c_str());
    env->ReleaseStringUTFChars(username, name);
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_setAuthInfo(
        JNIEnv* env,
        jobject thiz,
        jstring accessToken,
        jstring uuid,
        jstring tokenType) {

    const char* token = env->GetStringUTFChars(accessToken, nullptr);
    const char* id = env->GetStringUTFChars(uuid, nullptr);
    const char* type = env->GetStringUTFChars(tokenType, nullptr);

    ClientEngine::setPendingAuth(token, id, type);
    bool isPremium = !std::string(token).empty();

    JNI_LOGI("Auth info set: premium=%d, uuid=%s", isPremium, id);

    // 如果会话已存在，同步进去
    if (ClientEngine::getInstance() && ClientEngine::getInstance()->getGame()) {
        ClientEngine::getInstance()->getGame()->setAuthInfo(token, id, type);
    }

    env->ReleaseStringUTFChars(accessToken, token);
    env->ReleaseStringUTFChars(uuid, id);
    env->ReleaseStringUTFChars(tokenType, type);
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_onTouchEventImGui(
        JNIEnv* env,
        jobject thiz,
        jint pointerId,
        jfloat x,
        jfloat y,
        jint action) {

    auto& ui = GameUI::getInstance();
    if (ui.getState() == UIState::IN_GAME) {
        ui.onTouchEvent((int)pointerId, (float)x, (float)y, (int)action);
    } else {
        // 菜单模式只处理第一个触摸点（模拟鼠标）
        if (pointerId == 0) {
            ui.queueTouchEvent((float)x, (float)y, (int)action);
        }
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_calcite_MainActivity_onBackPressedNative(
        JNIEnv* env,
        jobject thiz) {

    auto& ui = GameUI::getInstance();
    if (ui.getState() == UIState::IN_GAME) {
        if (ui.isVideoSettingsOpen()) {
            // 视频设置：返回选项界面
            ui.setVideoSettingsOpen(false);
        } else if (ui.isOptionsOpen()) {
            // 选项界面：返回游戏菜单
            ui.setOptionsOpen(false);
        } else if (ui.isGameMenuOpen()) {
            // 游戏菜单已打开：关闭菜单，返回游戏
            ui.setGameMenuOpen(false);
        } else {
            // 游戏中：打开游戏菜单
            ui.setGameMenuOpen(true);
        }
        return JNI_TRUE;
    }
    if (ui.getState() == UIState::MAIN_MENU) {
        if (ui.isVideoSettingsOpen()) {
            // 视频设置：返回选项界面
            ui.setVideoSettingsOpen(false);
            return JNI_TRUE;
        }
        if (ui.isOptionsOpen()) {
            // 选项界面：关闭返回主菜单
            ui.setOptionsOpen(false);
            return JNI_TRUE;
        }
        // 主菜单其他情况不处理
        return JNI_FALSE;
    }
    if (ui.getState() == UIState::MULTIPLAYER || ui.getState() == UIState::CONNECTING) {
        ui.setState(UIState::MAIN_MENU);
        ScreenManager::getInstance().setScreen(std::make_unique<TitleScreen>());
        return JNI_TRUE;
    }
    // MAIN_MENU 或 CONNECTING 时，不处理（让系统默认行为退出）
    return JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_calcite_MainActivity_isUIDisplayed(
        JNIEnv* env,
        jobject thiz) {

    return GameUI::getInstance().getState() != UIState::IN_GAME ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_addImGuiCharacter(
        JNIEnv* env,
        jobject thiz,
        jint c) {

    GameUI::getInstance().addInputCharacter((unsigned int)c);
}