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
static bool g_cameraSyncedToPlayer = false;  // 标记摄像机是否已同步到玩家位置

// Java 虚拟机和对象引用，用于回调
static JavaVM* g_jvm = nullptr;
static jobject g_mainActivityObj = nullptr;

// 摄像机状态
static float g_cameraX = 0.0f;
static float g_cameraY = 8.0f;
static float g_cameraZ = 15.0f;
static float g_pitch = -0.5f;  // 向下看约 30 度
static float g_yaw = 0.0f;

extern "C" JNIEXPORT jboolean JNICALL
Java_com_minecraft_MainActivity_connectToServer(
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

    while (g_rendering) {
        frameCount++;

        if (frameCount <= 5 || frameCount % 60 == 0) {
            JNI_LOGI("Rendering frame %d", frameCount);
        }

        if (g_glRenderer) {
            g_glRenderer->render(g_cameraX, g_cameraY, g_cameraZ, g_pitch, g_yaw);
        } else if (g_useVulkan && g_vulkanRenderer) {
            g_vulkanRenderer->render(g_cameraX, g_cameraY, g_cameraZ, g_pitch, g_yaw);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    JNI_LOGI("Render thread stopped after %d frames", frameCount);
}

extern "C" JNIEXPORT void JNICALL
Java_com_minecraft_MainActivity_initRenderer(
        JNIEnv* env, jobject thiz, jobject surface) {

    JNI_LOGI("=== initRenderer called ===");

    // 加载 BlockRegistry（只加载一次）
    static bool blockRegistryLoaded = false;
    if (!blockRegistryLoaded) {
        // 从 assets 目录加载 blocks.json
        std::string blocksJsonPath = "/data/data/com.minecraft/files/blocks.json";
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
            g_engine->setRenderer(g_glRenderer);
            JNI_LOGI("ChunkManager and renderer linked");
            
            // 如果已经收到玩家位置且尚未同步，将摄像机传送到玩家位置
            if (!g_cameraSyncedToPlayer && g_engine->hasPlayerPosition()) {
                g_cameraX = static_cast<float>(g_engine->getPlayerX());
                g_cameraY = static_cast<float>(g_engine->getPlayerY());
                g_cameraZ = static_cast<float>(g_engine->getPlayerZ());
                g_yaw = g_engine->getYaw();
                g_pitch = g_engine->getPitch();
                g_cameraSyncedToPlayer = true;
                JNI_LOGI("Camera teleported to player position: (%.2f, %.2f, %.2f), yaw=%.2f, pitch=%.2f",
                         g_cameraX, g_cameraY, g_cameraZ, g_yaw, g_pitch);
            } else if (g_cameraSyncedToPlayer) {
                JNI_LOGI("Camera already synced to player, keeping current position");
            } else {
                JNI_LOGI("Player position not received yet, using default camera position");
            }
        }

        JNI_LOGI("OpenGL ES renderer initialized successfully");
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
Java_com_minecraft_MainActivity_renderFrame(
        JNIEnv* env, jobject thiz) {

    if (!g_initialized) {
        return;
    }

    if (g_useVulkan && g_vulkanRenderer) {
        g_vulkanRenderer->render(g_cameraX, g_cameraY, g_cameraZ, g_pitch, g_yaw);
    } else if (g_glRenderer) {
        g_glRenderer->render(g_cameraX, g_cameraY, g_cameraZ, g_pitch, g_yaw);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_minecraft_MainActivity_cleanupRenderer(
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
Java_com_minecraft_MainActivity_setCameraPosition(
        JNIEnv* env, jobject thiz,
        jfloat x, jfloat y, jfloat z,
        jfloat pitch, jfloat yaw) {

    g_cameraX = x;
    g_cameraY = y;
    g_cameraZ = z;
    g_pitch = pitch;
    g_yaw = yaw;
}

// 从 ClientEngine 同步摄像机位置到玩家位置
extern "C" JNIEXPORT void JNICALL
Java_com_minecraft_MainActivity_syncCameraToPlayer(
        JNIEnv* env, jobject thiz) {
    
    if (g_engine && g_engine->hasPlayerPosition()) {
        g_cameraX = static_cast<float>(g_engine->getPlayerX());
        g_cameraY = static_cast<float>(g_engine->getPlayerY());
        g_cameraZ = static_cast<float>(g_engine->getPlayerZ());
        g_yaw = g_engine->getYaw();
        g_pitch = g_engine->getPitch();
        JNI_LOGI("Camera synced to player: (%.2f, %.2f, %.2f), yaw=%.2f, pitch=%.2f",
                 g_cameraX, g_cameraY, g_cameraZ, g_yaw, g_pitch);
    } else {
        JNI_LOGE("Cannot sync camera: engine=%p, hasPosition=%s",
                 g_engine, g_engine ? (g_engine->hasPlayerPosition() ? "true" : "false") : "N/A");
    }
}

// 供 ClientEngine 调用的内部函数（自动同步）
void syncCameraToPlayerPosition(double x, double y, double z, float yaw, float pitch) {
    // 只同步一次
    if (g_cameraSyncedToPlayer) {
        JNI_LOGI("[AutoSync] Camera already synced, skipping");
        return;
    }
    
    g_cameraX = static_cast<float>(x);
    g_cameraY = static_cast<float>(y);
    g_cameraZ = static_cast<float>(z);
    g_yaw = yaw;
    g_pitch = pitch;
    g_cameraSyncedToPlayer = true;
    JNI_LOGI("[AutoSync] Camera synced to player: (%.2f, %.2f, %.2f), yaw=%.2f, pitch=%.2f",
             g_cameraX, g_cameraY, g_cameraZ, g_yaw, g_pitch);
    
    // 同步 Java 层的摄像机位置
    if (g_jvm && g_mainActivityObj) {
        JNIEnv* env = nullptr;
        bool needDetach = false;
        
        // 获取 JNIEnv
        int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                needDetach = true;
            } else {
                JNI_LOGE("Failed to attach current thread");
                return;
            }
        } else if (status != JNI_OK) {
            JNI_LOGE("Failed to get JNIEnv");
            return;
        }
        
        // 获取 MainActivity 类和方法 ID
        jclass clazz = env->GetObjectClass(g_mainActivityObj);
        jmethodID methodId = env->GetMethodID(clazz, "updateJavaCameraPosition", "(FFFFF)V");
        
        if (methodId) {
            env->CallVoidMethod(g_mainActivityObj, methodId, 
                               static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
                               yaw, pitch);
            JNI_LOGI("Called updateJavaCameraPosition on Java side");
        } else {
            JNI_LOGE("Failed to find updateJavaCameraPosition method");
        }
        
        // 分离线程（如果需要）
        if (needDetach) {
            g_jvm->DetachCurrentThread();
        }
    } else {
        JNI_LOGI("g_jvm=%p, g_mainActivityObj=%p, cannot callback Java", g_jvm, g_mainActivityObj);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_minecraft_MainActivity_updateCameraAngle(
        JNIEnv* env, jobject thiz,
        jfloat pitch, jfloat yaw) {

    g_pitch = pitch;
    g_yaw = yaw;
}

extern "C" JNIEXPORT void JNICALL
Java_com_minecraft_MainActivity_setAssetManager(
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
Java_com_minecraft_MainActivity_resizeRenderer(
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
Java_com_minecraft_MainActivity_setRendererType(
        JNIEnv* env,
        jobject thiz,
        jboolean useVulkan) {

    g_useVulkan = (bool)useVulkan;
    g_rendererTypeSet = true;
    JNI_LOGI("Renderer type set to: %s", useVulkan ? "Vulkan" : "OpenGL ES");
}

extern "C" JNIEXPORT void JNICALL
Java_com_minecraft_MainActivity_setProtocolVersion(
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
Java_com_minecraft_MainActivity_addBlock(
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
Java_com_minecraft_MainActivity_removeBlock(
        JNIEnv* env,
        jobject thiz,
        jint x,
        jint y,
        jint z) {

    if (g_glRenderer) {
        g_glRenderer->removeBlock((int)x, (int)y, (int)z);
    }
}
