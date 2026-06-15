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
#include "ResourcepackManager.h"
#include "MinecraftVersion.h"
#include "BlockRegistry.h"
#include "CameraController.h"
#include "Collision.h"
#include "Light.h"
#include "GameUI.h"
#include "imgui.h"

#define JNI_LOG_TAG "JNI"
#define JNI_LOGI(...) __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG, __VA_ARGS__)
#define JNI_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG, __VA_ARGS__)

static VulkanRenderer* g_vulkanRenderer = nullptr;
GLRenderer* g_glRenderer = nullptr;
static bool g_useVulkan = false;
static bool g_rendererTypeSet = false;  // 标记是否已设置渲染器类型
static ClientEngine* g_engine = nullptr;
static bool g_initialized = false;
static std::atomic<bool> g_rendering(false);

static std::thread g_renderThread;
static std::string g_username = "Player";

// 正版认证信息
static std::string g_accessToken;
static std::string g_playerUuid;
static std::string g_tokenType;
static bool g_isPremium = false;

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

    // 在后台线程中启动网络连接，避免阻塞主线程
    std::thread([addr = std::string(addr), port, name = std::string(name)]() {
        if (g_engine) {
            delete g_engine;
            g_engine = nullptr;
        }

                g_engine = new ClientEngine();

        // 传递正版认证信息给引擎
        if (g_isPremium) {
            g_engine->setAuthInfo(g_accessToken, g_playerUuid, g_tokenType);
        }

        // 加载语言文件
        {
            std::string langJson = TextureLoader::readTextFromZip("lang/zh_cn.json");
            if (!langJson.empty()) {
                g_engine->loadLanguage(langJson);
            } else {
                JNI_LOGI("No lang/zh_cn.json found in resourcepack");
            }
        }

        // 先设置 ChunkManager，让 render 线程能从 g_engine 中获取
        if (g_glRenderer) {
            g_engine->setRenderer(g_glRenderer);
        }
        GameUI::getInstance().setState(UIState::IN_GAME);

        g_engine->start(addr, port, name);

        // 断开连接后回到服务器列表
        JNI_LOGI("Disconnected, returning to server list");
        if (g_glRenderer) {
            g_glRenderer->clearChunks();
        }
        GameUI::getInstance().setState(UIState::MULTIPLAYER);
        GameUI::getInstance().setGameMenuOpen(false);
    }).detach();

    env->ReleaseStringUTFChars(address, addr);
    env->ReleaseStringUTFChars(username, name);

    JNI_LOGI("Connection thread started");
    return JNI_TRUE;
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

        // 确保 PlayerController 和 Light 持有 ChunkManager 引用
        if (g_engine) {
            auto* cm = g_engine->getChunkManager();
            if (cm) {
                if (!Collision::getInstance().hasChunkManager()) {
                    Collision::getInstance().setChunkManager(cm);
                    JNI_LOGI("PlayerController: ChunkManager acquired from engine");
                }
                Light::getInstance().setChunkManager(cm);
            }
        }

        // 背包/菜单/死亡界面打开时，重置移动输入但不中断物理（玩家仍受重力下落）
        if (g_engine && GameUI::getInstance().getState() == UIState::IN_GAME
            && GameUI::getInstance().isInGameUIActive()) {
            Collision::getInstance().resetMovement();
        }

        // 更新玩家物理（传入视角方向计算移动）
        float camPitch = CameraController::getInstance().getPitch();
        float camYaw = CameraController::getInstance().getYaw();
        glm::vec3 physPos;
        bool onGround = false;
        Collision::getInstance().update(deltaTime, camPitch, camYaw, &physPos, &onGround);

        // 发送玩家移动数据包到服务器（使用精确物理位置，已原子读取，无竞态）
        if (g_engine) {
            float curPitch = CameraController::getInstance().getPitch();
            float curYaw = CameraController::getInstance().getYaw();
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
            // 检查是否有已完成的光照重算，标记邻近 chunk
            {
                int lx, ly, lz;
                if (Light::getInstance().pollCompletedLightRecalc(&lx, &ly, &lz)) {
                    if (g_glRenderer) {
                        int cx = lx >> 4;
                        int cz = lz >> 4;
                        for (int dx = -2; dx <= 2; dx++) {
                            for (int dz = -2; dz <= 2; dz++) {
                                g_glRenderer->markChunkForUpdate(cx + dx, cz + dz);
                            }
                        }
                    }
                }
            }

            g_glRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);

            // 每帧检查 ImGui 是否需要键盘输入
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
    }
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
        // 从 ZIP 压缩包读取 blocks.json
        std::string blocksJsonContent = TextureLoader::readTextFromZip("blocks.json");
        if (!blocksJsonContent.empty() && BlockRegistry::getInstance().loadFromJson(blocksJsonContent)) {
            JNI_LOGI("BlockRegistry loaded successfully: %zu blocks",
                     BlockRegistry::getInstance().getBlockCount());
        } else {
            JNI_LOGE("Failed to load BlockRegistry, using fallback mapping");
        }

        // 加载物品 ID→名称映射（从 ZIP 压缩包读取 items.json）
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
            Light::getInstance().setChunkManager(g_engine->getChunkManager());
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

    // 初始化后应用保存的 FOV 和渲染距离设置
    if (g_glRenderer) {
        g_glRenderer->setFov(GameUI::getInstance().getOptionsFov());
        JNI_LOGI("Applied saved FOV: %.0f", GameUI::getInstance().getOptionsFov());

        g_glRenderer->setRenderDistance(GameUI::getInstance().getRenderDistance());
        JNI_LOGI("Applied saved render distance: %d", GameUI::getInstance().getRenderDistance());
    }

    // 设置 ImGui 连接回调（在任意渲染器初始化后）
    if (g_glRenderer) {
        GameUI::getInstance().setConnectCallback([](const std::string& ip, int port) {
            JNI_LOGI("UI Connect: %s:%d user=%s", ip.c_str(), port, g_username.c_str());
            std::thread([ip, port]() {
                            g_engine = new ClientEngine();

                // 传递正版认证信息给引擎
                if (g_isPremium) {
                    g_engine->setAuthInfo(g_accessToken, g_playerUuid, g_tokenType);
                }

                // 加载语言文件
                {
                    std::string langJson = TextureLoader::readTextFromZip("lang/zh_cn.json");
                    if (!langJson.empty()) {
                        g_engine->loadLanguage(langJson);
                    }
                }

                if (g_glRenderer) {
                    g_glRenderer->setChunkManager(g_engine->getChunkManager());
                    Collision::getInstance().setChunkManager(g_engine->getChunkManager());
                    Light::getInstance().setChunkManager(g_engine->getChunkManager());
                    g_engine->setRenderer(g_glRenderer);
                    JNI_LOGI("Engine and renderer linked");
                }

                // start() 会阻塞直到断开连接，所以先切换到 IN_GAME
                GameUI::getInstance().setState(UIState::IN_GAME);

                g_engine->start(ip, port, g_username);

                // 断开连接后回到服务器列表
                JNI_LOGI("Disconnected, returning to server list");
                if (g_glRenderer) {
                    g_glRenderer->clearChunks();
                }
                auto& ui = GameUI::getInstance();
                ui.setState(UIState::MULTIPLAYER);
                ui.setGameMenuOpen(false);
            }).detach();
        });

        // 设置游戏内菜单断开连接回调
        GameUI::getInstance().setDisconnectCallback([]() {
            JNI_LOGI("In-game disconnect requested");
            if (g_engine) {
                g_engine->disconnect();
            }
        });

        // 设置 FOV 更新回调
        GameUI::getInstance().setFovCallback([](float fov) {
            if (g_glRenderer) {
                g_glRenderer->setFov(fov);
            }
        });

        // 设置渲染距离更新回调
        GameUI::getInstance().setRenderDistanceCallback([](int chunks) {
            if (g_glRenderer) {
                g_glRenderer->setRenderDistance(chunks);
            }
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
    } else if (g_glRenderer) {
        g_glRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_cleanupRenderer(
        JNIEnv* env,
        jobject thiz) {

    JNI_LOGI("=== cleanupRenderer called ===");

    // 清除 GL 纹理缓存，防止切回前台时纹理 ID 失效变黑
    ResourcepackManager::getInstance().clear();

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

    // 释放 Activity 全局引用，防止内存泄漏
    if (g_mainActivityObj) {
        env->DeleteGlobalRef(g_mainActivityObj);
        g_mainActivityObj = nullptr;
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
Java_com_calcite_MainActivity_setAuthInfo(
        JNIEnv* env,
        jobject thiz,
        jstring accessToken,
        jstring uuid,
        jstring tokenType) {

    const char* token = env->GetStringUTFChars(accessToken, nullptr);
    const char* id = env->GetStringUTFChars(uuid, nullptr);
    const char* type = env->GetStringUTFChars(tokenType, nullptr);

    g_accessToken = token;
    g_playerUuid = id;
    g_tokenType = type;
    g_isPremium = !g_accessToken.empty();

    JNI_LOGI("Auth info set: premium=%d, uuid=%s", g_isPremium, g_playerUuid.c_str());

    env->ReleaseStringUTFChars(accessToken, token);
    env->ReleaseStringUTFChars(uuid, id);
    env->ReleaseStringUTFChars(tokenType, type);
}

/**
 * 通过 JNI 调用 Java 层处理完整的加密请求
 * Java 层负责：生成共享密钥 + SHA1 哈希 + Session Join + RSA 加密
 * 一次性完成所有加密准备工作，C++ 只需要拿到结果
 *
 * @param serverID         服务器 ID
 * @param publicKey        服务器公钥（DER 编码）
 * @param verifyToken      服务器验证令牌
 * @param sharedSecret     [输出] 原始共享密钥（16字节，用于初始化 AES CFB8）
 * @param encryptedSecret  [输出] RSA 加密后的共享密钥（发送给服务器）
 * @param encryptedVerifyToken [输出] RSA 加密后的验证令牌（发送给服务器）
 * @return 是否成功
 */
bool callJavaHandleEncryptionRequest(
    const std::string& serverID,
    const std::vector<unsigned char>& publicKey,
    const std::vector<unsigned char>& verifyToken,
    std::vector<unsigned char>& sharedSecret,
    std::vector<unsigned char>& encryptedSecret,
    std::vector<unsigned char>& encryptedVerifyToken) {

    if (!g_jvm || !g_mainActivityObj) {
        JNI_LOGE("Cannot handle encryption request: JVM or Activity not available");
        return false;
    }

    JNIEnv* env;
    bool attached = false;
    int getEnvResult = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (getEnvResult == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    } else if (getEnvResult != JNI_OK) {
        return false;
    }

    // 创建 Java 参数
    jstring jServerID = env->NewStringUTF(serverID.c_str());

    jbyteArray jPublicKey = env->NewByteArray(static_cast<jsize>(publicKey.size()));
    env->SetByteArrayRegion(jPublicKey, 0, static_cast<jsize>(publicKey.size()),
                            reinterpret_cast<const jbyte*>(publicKey.data()));

    jbyteArray jVerifyToken = env->NewByteArray(static_cast<jsize>(verifyToken.size()));
    env->SetByteArrayRegion(jVerifyToken, 0, static_cast<jsize>(verifyToken.size()),
                            reinterpret_cast<const jbyte*>(verifyToken.data()));

    jstring jAccessToken = env->NewStringUTF(g_accessToken.c_str());
    jstring jPlayerUuid = env->NewStringUTF(g_playerUuid.c_str());

    // 调用 MainActivity.handleEncryptionRequest
    jclass clazz = env->GetObjectClass(g_mainActivityObj);
    jmethodID method = env->GetMethodID(clazz, "handleEncryptionRequest",
        "(Ljava/lang/String;[B[BLjava/lang/String;Ljava/lang/String;)[B");

    jbyteArray jResult = nullptr;
    if (method) {
        jResult = (jbyteArray)env->CallObjectMethod(g_mainActivityObj, method,
            jServerID, jPublicKey, jVerifyToken, jAccessToken, jPlayerUuid);

        if (env->ExceptionCheck()) {
            JNI_LOGE("Java handleEncryptionRequest threw exception");
            env->ExceptionDescribe();
            env->ExceptionClear();
            jResult = nullptr;
        }
    } else {
        JNI_LOGE("handleEncryptionRequest method not found");
    }

    bool success = false;
    if (jResult != nullptr) {
        // 解析返回的打包字节数组
        // 格式：[4字节 sharedSecret_len][sharedSecret][4字节 encryptedSecret_len][encryptedSecret][4字节 encryptedVerifyToken_len][encryptedVerifyToken]
        jsize resultLen = env->GetArrayLength(jResult);
        jbyte* resultBytes = env->GetByteArrayElements(jResult, nullptr);

        const uint8_t* data = reinterpret_cast<const uint8_t*>(resultBytes);
        size_t offset = 0;

        if (resultLen >= 4) {
            // 读取 sharedSecret
            int32_t ssLen = (data[offset] << 24) | (data[offset+1] << 16) | (data[offset+2] << 8) | data[offset+3];
            offset += 4;
            if (offset + ssLen <= (size_t)resultLen && ssLen > 0) {
                sharedSecret.assign(data + offset, data + offset + ssLen);
                offset += ssLen;
            }

            // 读取 encryptedSecret
            if (offset + 4 <= (size_t)resultLen) {
                int32_t esLen = (data[offset] << 24) | (data[offset+1] << 16) | (data[offset+2] << 8) | data[offset+3];
                offset += 4;
                if (offset + esLen <= (size_t)resultLen && esLen > 0) {
                    encryptedSecret.assign(data + offset, data + offset + esLen);
                    offset += esLen;
                }
            }

            // 读取 encryptedVerifyToken
            if (offset + 4 <= (size_t)resultLen) {
                int32_t evtLen = (data[offset] << 24) | (data[offset+1] << 16) | (data[offset+2] << 8) | data[offset+3];
                offset += 4;
                if (offset + evtLen <= (size_t)resultLen && evtLen > 0) {
                    encryptedVerifyToken.assign(data + offset, data + offset + evtLen);
                    offset += evtLen;
                }
            }

            success = !sharedSecret.empty() && !encryptedSecret.empty() && !encryptedVerifyToken.empty();
        }

        env->ReleaseByteArrayElements(jResult, resultBytes, JNI_ABORT);
        JNI_LOGI("Encryption request handled: sharedSecret_len=%zu, encSecret_len=%zu, encVerifyToken_len=%zu",
                 sharedSecret.size(), encryptedSecret.size(), encryptedVerifyToken.size());
    } else {
        JNI_LOGE("handleEncryptionRequest returned null");
    }

    // 清理 JNI 引用
    env->DeleteLocalRef(jServerID);
    env->DeleteLocalRef(jPublicKey);
    env->DeleteLocalRef(jVerifyToken);
    env->DeleteLocalRef(jAccessToken);
    env->DeleteLocalRef(jPlayerUuid);
    if (jResult) env->DeleteLocalRef(jResult);
    env->DeleteLocalRef(clazz);

    if (attached) {
        g_jvm->DetachCurrentThread();
    }

    return success;
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
    if (ui.getState() == UIState::MULTIPLAYER) {
        ui.setState(UIState::MAIN_MENU);
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