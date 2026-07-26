#include <jni.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <cstring>
#include "ClientEngine/ClientEngine.h"
#include "ClientEngine/GameEngine.h"
#include "Renderer/VulkanRenderer.h"
#include "Renderer/GLRenderer.h"
#include "utils.h"
#include "TextureLoader.h"
#include "ResourcepackManager.h"
#include "Collision.h"
#include "gui/GameUI.h"
#include "JniBridge.h"
#include "gui/ScreenManager.h"
#include "gui/TitleScreen.h"

#define JNI_LOG_TAG "JNI"
#define JNI_LOGI(...) __android_log_print(ANDROID_LOG_INFO, JNI_LOG_TAG, __VA_ARGS__)
#define JNI_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, JNI_LOG_TAG, __VA_ARGS__)

static bool g_initialized = false;

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_initClient(
        JNIEnv* env, jobject thiz, jobject surface) {

    JNI_LOGI("=== initClient called ===");

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

    // 3. 初始化渲染器（内部根据渲染器类型创建 GL/Vulkan，GL 路径含 loadBlockRegistry）
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

    // 5. 启动渲染线程（由 ClientEngine 管理生命周期）
    client->startRenderThread();

    JNI_LOGI("=== initClient completed ===");
}


extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_cleanupRenderer(
        JNIEnv* env,
        jobject thiz) {

    JNI_LOGI("=== cleanupRenderer called ===");

    // 1. 先停渲染线程（确保 EGL context 不再被使用）
    if (ClientEngine::getInstance()) {
        ClientEngine::getInstance()->stopRenderThread();
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

    // 4. 删除 ClientEngine（内部会销毁 GL/Vulkan 渲染器和会话）
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
        // Vulkan 模式：标记 Surface 失效，渲染线程跳过提交
        if (auto* vkRenderer = client->getVulkanRenderer()) {
            vkRenderer->invalidateSurface();
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

        // Vulkan 模式：直接重建 Surface + Swapchain（渲染线程此时因 surfaceValid=false 空转）
        if (auto* vkRenderer = client->getVulkanRenderer()) {
            int w = ANativeWindow_getWidth(window);
            int h = ANativeWindow_getHeight(window);
            vkRenderer->recreateSurface(window, w, h);
            ANativeWindow_release(window);
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
    VulkanRenderer* vkRenderer = ClientEngine::getInstance() ? ClientEngine::getInstance()->getVulkanRenderer() : nullptr;

    if (glRenderer) {
        glRenderer->recreateSurface(width, height);
    } else if (vkRenderer) {
        vkRenderer->recreateSwapchain(width, height);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_calcite_MainActivity_setRendererType(
        JNIEnv* env,
        jobject thiz,
        jstring rendererType) {

    const char* type = env->GetStringUTFChars(rendererType, nullptr);
    if (strcmp(type, "vulkan") == 0) {
        ClientEngine::setRendererType(ClientEngine::RendererType::Vulkan);
    } else {
        // 未知值回退 OpenGL ES，保证总能启动
        ClientEngine::setRendererType(ClientEngine::RendererType::OpenGL);
    }
    JNI_LOGI("Renderer type set to: %s", type);
    env->ReleaseStringUTFChars(rendererType, type);
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