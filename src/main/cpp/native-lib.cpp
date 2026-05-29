#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <thread>
#include <atomic>
#include "ClientEngine.h"
#include "VulkanRenderer.h"
#include "GLRenderer.h"
#include "utils.h"
#include "TextureLoader.h"
#include "MinecraftVersion.h"
#include "BlockRegistry.h"
#include "CameraController.h"
#include "Collision.h"
#include "GameUI.h"

#define JNI_LOG_TAG "JNI"
#define JNI_LOGI(...) __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG, __VA_ARGS__)
#define JNI_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG, __VA_ARGS__)

static VulkanRenderer* g_vulkanRenderer = nullptr;
static GLRenderer* g_glRenderer = nullptr;
static bool g_useVulkan = false;
static bool g_rendererTypeSet = false;  // 标记是否已设置渲染器类型
static ClientEngine* g_engine = nullptr;
static bool g_initialized = false;
static std::atomic<bool> g_rendering(false);
static std::thread g_renderThread;
static std::string g_username = "Player";

// Java 虚拟机和对象引用，用于回调
static JavaVM* g_jvm = nullptr;
static jobject g_mainActivityObj = nullptr;

extern "C" JNIEXPORT jboolean JNICALL
Java_com_calcite_MainActivity_connectToServer(
        JNIEnv* env,
        jobject thiz,
        jstring address,
        jint port,
        jstring username) {

    const char* addr = env->GetStringUTFChars(address, nullptr);
    const char* name = env->GetStringUTFChars(username, nullptr);

    JNI_LOGI("Connecting to %s:%d as %s", addr, port, name);

    // 保存 JavaVM 和 MainActivity 对象引用
    env->GetJavaVM(&g_jvm);
    if (g_mainActivityObj) {
        env->DeleteGlobalRef(g_mainActivityObj);
    }
    g_mainActivityObj = env->NewGlobalRef(thiz);
    JNI_LOGI("Saved JavaVM=%p and MainActivity global ref", g_jvm);

    if (g_engine) {
        delete g_engine;
        g_engine = nullptr;
    }

    g_engine = new ClientEngine();
    bool success = g_engine->start(addr, port, name);

    env->ReleaseStringUTFChars(address, addr);
    env->ReleaseStringUTFChars(username, name);

    JNI_LOGI("Connection result: %s", success ? "success" : "failed");
    return success ? JNI_TRUE : JNI_FALSE;
}

static void renderLoop() {
    JNI_LOGI("Render thread started");

    // 在渲染线程中重新绑定 EGL context
    if (g_glRenderer) {
        g_glRenderer->makeCurrent();
    }

    int frameCount = 0;
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (g_rendering) {
        frameCount++;

        // 计算 delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        // 确保 PlayerController 持有 ChunkManager 引用
        if (g_engine && !Collision::getInstance().hasChunkManager()) {
            auto* cm = g_engine->getChunkManager();
            if (cm) {
                Collision::getInstance().setChunkManager(cm);
                JNI_LOGI("PlayerController: ChunkManager acquired from engine");
            }
        }

        // 更新玩家物理（传入视角方向计算移动）
        float camPitch = CameraController::getInstance().getPitch();
        float camYaw = CameraController::getInstance().getYaw();
        Collision::getInstance().update(deltaTime, camPitch, camYaw);

        // 发送玩家移动数据包到服务器（使用精确物理位置，而非插值位置）
        if (g_engine) {
            auto physPos = Collision::getInstance().getPosition();
            float curPitch = CameraController::getInstance().getPitch();
            float curYaw = CameraController::getInstance().getYaw();
            bool onGround = Collision::getInstance().isOnGround();
            g_engine->sendPlayerMovement(physPos.x, physPos.y, physPos.z, curYaw, curPitch, onGround);
        }

        if (frameCount <= 5 || frameCount % 60 == 0) {
            JNI_LOGI("Rendering frame %d", frameCount);
        }

        // 从 CameraController 获取摄像机数据
        auto pos = CameraController::getInstance().getSmoothPosition();
        float pitch = CameraController::getInstance().getPitch();
        float yaw = CameraController::getInstance().getYaw();

        if (g_glRenderer) {
            g_glRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);

            // 检查 ImGui 是否需要键盘输入，通过 JNI 直接调用
            static bool lastWantTextInput = false;
            bool wantText = GameUI::getInstance().wantsTextInput();
            if (wantText != lastWantTextInput) {
                lastWantTextInput = wantText;
                if (g_jvm && g_mainActivityObj) {
                    JNIEnv* env;
                    bool attached = false;
                    int ger = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
                    if (ger == JNI_EDETACHED) {
                        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true;
                        else continue;
                    } else if (ger != JNI_OK) {
                        continue;
                    }
                    jclass clazz = env->GetObjectClass(g_mainActivityObj);
                    jmethodID method = env->GetMethodID(clazz, "showKeyboardImGui", "(Z)V");
                    if (method) {
                        env->CallVoidMethod(g_mainActivityObj, method, wantText);
                    }
                    env->DeleteLocalRef(clazz);
                    if (attached) g_jvm->DetachCurrentThread();
                }
            }
        } else if (g_useVulkan && g_vulkanRenderer) {
            g_vulkanRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    JNI_LOGI("Render thread stopped after %d frames", frameCount);
}

// 辅助函数：通过 JNI 调用 Java 层的 UI 方法（自动附加线程）
static void callJavaVoidMethod(const char* methodName, const char* signature) {
    if (!g_jvm || !g_mainActivityObj) return;
    JNIEnv* env;
    bool attached = false;
    int getEnvResult = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvResult == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    } else if (getEnvResult != JNI_OK) {
        return;
    }
    jclass clazz = env->GetObjectClass(g_mainActivityObj);
    jmethodID method = env->GetMethodID(clazz, methodName, signature);
    if (method) {
        env->CallVoidMethod(g_mainActivityObj, method);
    }
    env->DeleteLocalRef(clazz);
    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_initRenderer(
        JNIEnv* env, jobject thiz, jobject surface) {

    JNI_LOGI("=== initRenderer called ===");

    // 保存 JavaVM 和 Activity 引用（用于回调控制 UI）
    env->GetJavaVM(&g_jvm);
    if (g_mainActivityObj) {
        env->DeleteGlobalRef(g_mainActivityObj);
    }
    g_mainActivityObj = env->NewGlobalRef(thiz);

    // 加载 BlockRegistry（只加载一次）
    static bool blockRegistryLoaded = false;
    if (!blockRegistryLoaded) {
        // 从 assets 目录加载 blocks.json
        std::string blocksJsonPath = "/data/data/com.calcite/blocks.json";
        JNI_LOGI("Loading BlockRegistry from: %s", blocksJsonPath.c_str());

        if (BlockRegistry::getInstance().loadFromJson(blocksJsonPath)) {
            JNI_LOGI("BlockRegistry loaded successfully: %zu blocks",
                     BlockRegistry::getInstance().getBlockCount());
        } else {
            JNI_LOGE("Failed to load BlockRegistry, using fallback mapping");
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
        g_glRenderer = new GLRenderer();

        if (!g_glRenderer->initialize(window)) {
            JNI_LOGE("Failed to initialize OpenGL renderer");
            delete g_glRenderer;
            g_glRenderer = nullptr;
            ANativeWindow_release(window);
            return;
        }

        // 设置 ChunkManager 和渲染器引用
        if (g_engine) {
            g_glRenderer->setChunkManager(g_engine->getChunkManager());
            Collision::getInstance().setChunkManager(g_engine->getChunkManager());
            g_engine->setRenderer(g_glRenderer);
            JNI_LOGI("ChunkManager and renderer linked");

            // 如果已经收到玩家位置，将摄像机传送到玩家位置
            if (g_engine->hasPlayerPosition()) {
                float playerX = static_cast<float>(g_engine->getPlayerX());
                float playerY = static_cast<float>(g_engine->getPlayerY());
                float playerZ = static_cast<float>(g_engine->getPlayerZ());
                float playerYaw = g_engine->getYaw();
                float playerPitch = g_engine->getPitch();

                CameraController::getInstance().setPosition(playerX, playerY, playerZ);
                CameraController::getInstance().setRotation(playerPitch, playerYaw);

                JNI_LOGI("Camera teleported to player position: (%.2f, %.2f, %.2f), yaw=%.2f, pitch=%.2f",
                         playerX, playerY, playerZ, playerYaw, playerPitch);
            } else {
                JNI_LOGI("Player position not received yet, using default camera position");
            }
        }

        JNI_LOGI("OpenGL ES renderer initialized successfully");
    }

    // 设置 ImGui 连接回调（在任意渲染器初始化后）
    if (g_glRenderer) {
        GameUI::getInstance().setConnectCallback([](const std::string& ip, int port) {
            JNI_LOGI("UI Connect: %s:%d user=%s", ip.c_str(), port, g_username.c_str());
            std::thread([ip, port]() {
                g_engine = new ClientEngine();
                if (g_glRenderer) {
                    g_glRenderer->setChunkManager(g_engine->getChunkManager());
                    Collision::getInstance().setChunkManager(g_engine->getChunkManager());
                    g_engine->setRenderer(g_glRenderer);
                    JNI_LOGI("Engine and renderer linked");
                }

                // start() 会阻塞直到断开连接，所以先切换到 IN_GAME
                GameUI::getInstance().setState(UIState::IN_GAME);

                bool success = g_engine->start(ip, port, g_username);

                if (!success) {
                    // 登录失败，回到多人游戏菜单
                    JNI_LOGE("Connection failed, returning to menu");
                    GameUI::getInstance().setState(UIState::MULTIPLAYER);
                } else {
                    // 正常断开连接，回到主菜单
                    JNI_LOGI("Disconnected, returning to main menu");
                    GameUI::getInstance().setState(UIState::MAIN_MENU);
                }
            }).detach();
        });

        // 设置退出游戏回调（返回 Java 启动器）
        GameUI::getInstance().setExitCallback([]() {
            JNIEnv* env;
            bool attached = false;
            int getEnvResult = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
            if (getEnvResult == JNI_EDETACHED) {
                if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
                attached = true;
            } else if (getEnvResult != JNI_OK) {
                return;
            }
            jclass clazz = env->GetObjectClass(g_mainActivityObj);
            jmethodID finishMethod = env->GetMethodID(clazz, "finish", "()V");
            if (finishMethod) {
                env->CallVoidMethod(g_mainActivityObj, finishMethod);
            }
            env->DeleteLocalRef(clazz);
            if (attached) {
                g_jvm->DetachCurrentThread();
            }
        });
    }

    g_initialized = true;
    ANativeWindow_release(window);

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

    if (!g_initialized) {
        return;
    }

    // 从 CameraController 获取摄像机数据
    auto pos = CameraController::getInstance().getSmoothPosition();
    float pitch = CameraController::getInstance().getPitch();
    float yaw = CameraController::getInstance().getYaw();

    if (g_useVulkan && g_vulkanRenderer) {
        g_vulkanRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
    } else if (g_glRenderer) {
        g_glRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_cleanupRenderer(
        JNIEnv* env,
        jobject thiz) {

    JNI_LOGI("=== cleanupRenderer called ===");

    if (g_rendering) {
        g_rendering = false;
        if (g_renderThread.joinable()) {
            JNI_LOGI("Waiting for render thread to finish");
            g_renderThread.join();
            JNI_LOGI("Render thread joined");
        }
    }

    if (g_vulkanRenderer) {
        delete g_vulkanRenderer;
        g_vulkanRenderer = nullptr;
    }

    if (g_glRenderer) {
        delete g_glRenderer;
        g_glRenderer = nullptr;
    }

    g_initialized = false;
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
        GLRenderer::setAssetManager(assets);
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

    if (g_glRenderer) {
        g_glRenderer->recreateSurface(width, height);
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
Java_com_calcite_MainActivity_setProtocolVersion(
        JNIEnv* env,
        jobject thiz,
        jint version) {

    int protocolVersion = (int)version;
    VersionManager::getInstance().setProtocolVersion(protocolVersion);

    JNI_LOGI("Protocol version set to: %d (%s)",
             protocolVersion,
             VersionManager::getInstance().getVersionName().c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_setUsername(
        JNIEnv* env,
        jobject thiz,
        jstring username) {

    const char* name = env->GetStringUTFChars(username, nullptr);
    g_username = name;
    JNI_LOGI("Username set: %s", g_username.c_str());
    env->ReleaseStringUTFChars(username, name);
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
    if (ui.getState() == UIState::MULTIPLAYER) {
        ui.setState(UIState::MAIN_MENU);
        return JNI_TRUE; // 已处理
    }
    // MAIN_MENU 或 CONNECTING 或 IN_GAME 时，不处理（让系统默认行为退出）
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

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_addBlock(
        JNIEnv* env,
        jobject thiz,
        jint x,
        jint y,
        jint z) {

    if (g_glRenderer) {
        g_glRenderer->addBlock((int)x, (int)y, (int)z);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_removeBlock(
        JNIEnv* env,
        jobject thiz,
        jint x,
        jint y,
        jint z) {

    if (g_glRenderer) {
        g_glRenderer->removeBlock((int)x, (int)y, (int)z);
    }
}