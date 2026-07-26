#pragma once
#include <string>
#include <map>
#include <memory>
#include <thread>
#include <atomic>
#include <jni.h>

struct ANativeWindow;  // 前向声明，避免引入 android/native_window.h

class GLRenderer;
class VulkanRenderer;
class GameEngine;
class EntityRenderer;
class TextureAtlas;
class BlockRegistry;
class MusicManager;
// 全局引擎（单例）：App 启动 → App 退出
// 持有渲染器、全局 UI/音频资源、当前会话（GameEngine）
class ClientEngine {
public:
    // 渲染后端类型（Java 层传入 "opengl"/"vulkan" 字符串，JNI 层转换为枚举）
    enum class RendererType { OpenGL, Vulkan };

    ClientEngine();
    ~ClientEngine();

    static ClientEngine* getInstance() { return instance; }

    // ===== 初始化 =====
    // 由 JNI 层在 Surface 准备好后调用，根据渲染器类型创建 GL/Vulkan 渲染器
    bool initializeRenderer(ANativeWindow* window);

    // ===== 渲染器类型（JNI 层在 initializeRenderer 之前写入） =====
    static void setRendererType(RendererType type) { s_rendererType = type; }
    static RendererType getRendererType() { return s_rendererType; }

    // ===== 渲染线程管理 =====
    // 启动渲染线程（initializeRenderer 成功后调用）
    void startRenderThread();
    // 停止并 join 渲染线程（销毁引擎前调用，幂等）
    void stopRenderThread();
    // 渲染线程已进入非游戏分支（安全销毁会话）
    bool isRenderThreadIdle() const { return m_renderThreadIdle.load(std::memory_order_acquire); }

    // ===== 玩家用户名（JNI 层写入，连接时读取） =====
    static void setUsername(const std::string& name) { s_username = name; }
    static const std::string& getUsername() { return s_username; }

    // ===== 暂存正版认证信息（JNI 层写入，连接回调读取） =====
    static void setPendingAuth(const std::string& accessToken, const std::string& uuid, const std::string& tokenType);
    static bool isPremiumPending();
    static const std::string& getPendingAccessToken();
    static const std::string& getPendingPlayerUuid();
    static const std::string& getPendingTokenType();
    void setupUICallbacks();
    // ===== 渲染器管理 =====
    void setRenderer(std::unique_ptr<GLRenderer> renderer);
    std::unique_ptr<GLRenderer> releaseRenderer();
    GLRenderer* getRenderer() { return m_renderer.get(); }

    // ===== Vulkan 渲染器（与 GLRenderer 互斥，第一步仅渲染主界面） =====
    VulkanRenderer* getVulkanRenderer() { return m_vulkanRenderer.get(); }

    // ===== 实体渲染器（OpenGL 组件，全局生命周期） =====
    EntityRenderer* getEntityRenderer() { return m_entityRenderer.get(); }

    // ===== 纹理图集（全局生命周期） =====
    TextureAtlas* getTextureAtlas() { return m_textureAtlas.get(); }

    // ===== 方块注册表（全局生命周期） =====
    BlockRegistry* getBlockRegistry() { return m_blockRegistry.get(); }
    /// 从 ZIP 加载 blocks.json + items.json（幂等，多次调用只加载一次）
    void loadBlockRegistry();

    // ===== 语言翻译表（客户端资源，全局生命周期） =====
    /// 从 ZIP 加载 lang/zh_cn.json（幂等，多次调用只加载一次）
    void loadLanguage();
    /// 查询翻译键，未命中返回 nullptr
    const std::string* translate(const std::string& key) const;

    // ===== 背景音乐/音效（全局生命周期） =====
    MusicManager* getMusicManager() { return m_musicManager.get(); }

    // ===== 会话管理 =====
    GameEngine* createGame();
    void destroyGame();
    GameEngine* getGame() { return m_gameEngine.get(); }

private:
    // 渲染线程主循环（在 m_renderThread 中运行）
    void renderLoop();

    std::unique_ptr<GLRenderer> m_renderer;
    std::unique_ptr<VulkanRenderer> m_vulkanRenderer;
    std::unique_ptr<EntityRenderer> m_entityRenderer;
    std::unique_ptr<TextureAtlas> m_textureAtlas;
    std::unique_ptr<BlockRegistry> m_blockRegistry;
    std::unique_ptr<GameEngine> m_gameEngine;
    std::unique_ptr<MusicManager> m_musicManager;
    static ClientEngine* instance;

    std::thread m_renderThread;
    std::atomic<bool> m_rendering{false};
    std::atomic<bool> m_renderThreadIdle{false};

    static std::string s_pendingAccessToken;
    static std::string s_pendingPlayerUuid;
    static std::string s_pendingTokenType;
    static std::string s_username;
    static RendererType s_rendererType;
    bool m_blockRegistryLoaded = false;

    // 语言翻译表（连接时惰性加载一次，之后只读）
    std::map<std::string, std::string> m_translations;
    bool m_languageLoaded = false;
};