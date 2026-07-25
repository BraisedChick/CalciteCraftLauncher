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
#include "CameraController.h"
#include "Collision.h"
#include "Light.h"
#include "gui/GameUI.h"
#include "MusicManager.h"
#include "imgui.h"
#include "gui/ConnectingScreen.h"
#include "JniBridge.h"
#include "gui/ScreenManager.h"
#include "gui/TitleScreen.h"

#define JNI_LOG_TAG "JNI"
#define JNI_LOGI(...) __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG, __VA_ARGS__)
#define JNI_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG, __VA_ARGS__)

static VulkanRenderer* g_vulkanRenderer = nullptr;
static bool g_useVulkan = false;
static bool g_initialized = false;
static bool g_rendererTypeSet = false;  // 标记是否已设置渲染器类型
static std::atomic<bool> g_rendering(false);
std::atomic<bool> g_renderThreadIdle(false);  // 渲染线程已进入非游戏分支（安全删除引擎）

static std::thread g_renderThread;

static void renderLoop() {
    JNI_LOGI("Render thread started");

    // 在渲染线程中重新绑定 EGL context
    auto* renderLoopRenderer = ClientEngine::getInstance() ? ClientEngine::getInstance()->getRenderer() : nullptr;
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
                    if (!game->getCollision()->hasChunkManager()) {
                        game->getCollision()->setChunkManager(cm);
                        JNI_LOGI("PlayerController: ChunkManager acquired from engine");
                    }
                    game->getLight()->setChunkManager(cm);
                }
            }

            // 背包/菜单/死亡界面打开时，重置移动输入但不中断物理（玩家仍受重力下落）
            if (game && GameUI::getInstance().isInGameUIActive()) {
                game->getCollision()->resetMovement();
            }

            // 更新玩家物理（传入视角方向计算移动）
            float camPitch = CameraController::getInstance().getPitch();
            float camYaw = CameraController::getInstance().getYaw();
            glm::vec3 physPos;
            bool onGround = false;
            game->getCollision()->update(deltaTime, camPitch, camYaw, &physPos, &onGround);

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
                    if (game->getLight()->pollCompletedLightRecalc(&lx, &ly, &lz)) {
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
            } else if (g_useVulkan && g_vulkanRenderer) {
                g_vulkanRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
            }
        } else {
            // 非游戏状态：不访问任何引擎资源，GLRenderer::render() 内部渲染全景+ImGui
            g_renderThreadIdle.store(true, std::memory_order_release);

            auto pos = CameraController::getInstance().getSmoothPosition();
            float pitch = CameraController::getInstance().getPitch();
            float yaw = CameraController::getInstance().getYaw();

            auto* activeRenderer = ClientEngine::getInstance() ? ClientEngine::getInstance()->getRenderer() : nullptr;
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
            auto* music = ClientEngine::getInstance()->getMusicManager();
            if (music->isInitialized()) {
                // 根据 UI 状态同步音乐场景
                UIState uiState = GameUI::getInstance().getState();
                MusicScene targetScene;
                if (uiState == UIState::IN_GAME) {
                    targetScene = MusicScene::GAME; // TODO: 创造模式判断
                } else {
                    targetScene = MusicScene::MENU;
                }
                if (music->getScene() != targetScene) {
                    music->setScene(targetScene);
                }
                music->tick();
            }
        }
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_initRenderer(
        JNIEnv* env, jobject thiz, jobject surface) {

    JNI_LOGI("=== initRenderer called ===");

    // 1. 保存 JavaVM 和 Activity 引用
    JavaVM* jvm;
    env->GetJavaVM(&jvm);
    JniBridge::init(jvm, env->NewGlobalRef(thiz));

    // 2. 创建 ClientEngine
    if (!ClientEngine::getInstance()) {
        new ClientEngine();
        JNI_LOGI("ClientEngine created");
    }
    auto* client = ClientEngine::getInstance();

    // 3. 初始化渲染器（内部完成 loadBlockRegistry + GLRenderer 创建）
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        JNI_LOGE("Failed to get ANativeWindow");
        return;
    }
    if (!client->initializeRenderer(window)) {
        JNI_LOGE("Failed to initialize renderer");
        ANativeWindow_release(window);
        return;
    }
    ANativeWindow_release(window);

    // 4. 设置 UI 回调（连接、退出等）
    client->setupUICallbacks();

    // 5. 启动渲染线程（g_rendering 等全局变量暂留，第二步迁移）
    JNI_LOGI("Starting render thread");
    g_rendering = true;
    g_renderThread = std::thread(renderLoop);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    JNI_LOGI("=== initRenderer completed ===");
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
    GLRenderer* glRenderer = ClientEngine::getInstance() ? ClientEngine::getInstance()->getRenderer() : nullptr;
    if (glRenderer) {
        if (glRenderer->hasValidSurface()) {
            glRenderer->makeCurrent();
        } else {
            // Surface 已释放（切屏后退出），创建临时 pbuffer 让 context 可用
            JNI_LOGI("Surface already released, creating temp pbuffer for cleanup");
            EGLConfig config = glRenderer->getEGLConfig();
            EGLDisplay disp = glRenderer->getEGLDisplay();
            EGLContext ctx = glRenderer->getEGLContext();
            if (disp != EGL_NO_DISPLAY && ctx != EGL_NO_CONTEXT && config) {
                EGLint pbufAttribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
                EGLSurface tempSurface = eglCreatePbufferSurface(disp, config, pbufAttribs);
                if (tempSurface != EGL_NO_SURFACE) {
                    eglMakeCurrent(disp, tempSurface, tempSurface, ctx);
                    JNI_LOGI("Temp pbuffer created and context made current");
                } else {
                    JNI_LOGE("Failed to create temp pbuffer: 0x%x", eglGetError());
                }
            }
        }
    }

    // 3. 清除 GL 纹理缓存（现在 context 是当前的，glDeleteTextures 有效）
    ResourcepackManager::getInstance().clear();

    // 4. 删除渲染器（内部 cleanup 会销毁 EGL context）
    if (g_vulkanRenderer) {
        delete g_vulkanRenderer;
        g_vulkanRenderer = nullptr;
    }

    // 删除 ClientEngine（内部会销毁渲染器和会话）
    if (ClientEngine::getInstance()) {
        delete ClientEngine::getInstance();
    }
    // 释放 Activity 全局引用，防止内存泄漏
    JniBridge::cleanup(env);

    g_initialized = false;

    JNI_LOGI("Cleanup completed");
}

// 切屏出去时调用（请求释放 Surface，不销毁引擎）
// 注意：EGL context 绑定在渲染线程，此处仅发起请求，实际释放由渲染线程执行
extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_onSurfaceReleased(
        JNIEnv* env, jobject thiz) {
    JNI_LOGI("onSurfaceReleased: requesting EGL Surface release");
    auto* client = ClientEngine::getInstance();
    if (client) {
        auto* renderer = client->getRenderer();
        if (renderer) {
            renderer->requestSurfaceRelease();
        }
    }
    // 渲染线程继续运行，render() 内部检测到 Surface 无效会跳过
}

// 切屏回来时调用（请求重建 Surface）
// 注意：EGL context 绑定在渲染线程，此处仅发起请求并转移 window 所有权
extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_onSurfaceRecreated(
        JNIEnv* env, jobject thiz, jobject surface) {
    JNI_LOGI("onSurfaceRecreated: requesting EGL Surface recreation");
    if (!surface) return;

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) return;

    auto* client = ClientEngine::getInstance();
    if (client) {
        auto* renderer = client->getRenderer();
        if (renderer) {
            renderer->requestSurfaceRecreate(window);  // 所有权转移，渲染线程处理完负责 release
            return;
        }
    }

    // 没有 renderer 时直接释放，避免泄露
    ANativeWindow_release(window);
}

// ===== 新的输入控制接口 =====

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_setKeyState(
        JNIEnv* env, jobject thiz,
        jint key, jboolean pressed) {

    auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (game && game->getCollision()) game->getCollision()->setKeyState((int)key, (bool)pressed);
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

    GLRenderer* glRenderer = ClientEngine::getInstance() ? ClientEngine::getInstance()->getRenderer() : nullptr;

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

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_addImGuiCharacter(
        JNIEnv* env,
        jobject thiz,
        jint c) {

    GameUI::getInstance().addInputCharacter((unsigned int)c);
}