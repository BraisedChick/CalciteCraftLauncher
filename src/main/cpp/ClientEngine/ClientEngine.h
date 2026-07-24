#pragma once
#include <string>
#include <memory>
#include <jni.h>

struct ANativeWindow;  // 前向声明，避免引入 android/native_window.h

class GLRenderer;
class GameEngine;
class EntityRenderer;
class TextureAtlas;
class BlockRegistry;
class MusicManager;
// 全局引擎（单例）：App 启动 → App 退出
// 持有渲染器、全局 UI/音频资源、当前会话（GameEngine）
class ClientEngine {
public:
    ClientEngine();
    ~ClientEngine();

    static ClientEngine* getInstance() { return instance; }

    // ===== 初始化 =====
    // 由 JNI 层在 Surface 准备好后调用，创建渲染器
    bool initializeRenderer(ANativeWindow* window);

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

    // ===== 实体渲染器（OpenGL 组件，全局生命周期） =====
    EntityRenderer* getEntityRenderer() { return m_entityRenderer.get(); }

    // ===== 纹理图集（全局生命周期） =====
    TextureAtlas* getTextureAtlas() { return m_textureAtlas.get(); }

    // ===== 方块注册表（全局生命周期） =====
    BlockRegistry* getBlockRegistry() { return m_blockRegistry.get(); }
    /// 从 ZIP 加载 blocks.json + items.json（幂等，多次调用只加载一次）
    void loadBlockRegistry();

    // ===== 背景音乐/音效（全局生命周期） =====
    MusicManager* getMusicManager() { return m_musicManager.get(); }

    // ===== 会话管理 =====
    GameEngine* createGame();
    void destroyGame();
    GameEngine* getGame() { return m_gameEngine.get(); }

private:
    std::unique_ptr<GLRenderer> m_renderer;
    std::unique_ptr<EntityRenderer> m_entityRenderer;
    std::unique_ptr<TextureAtlas> m_textureAtlas;
    std::unique_ptr<BlockRegistry> m_blockRegistry;
    std::unique_ptr<GameEngine> m_gameEngine;
    std::unique_ptr<MusicManager> m_musicManager;
    static ClientEngine* instance;

    static std::string s_pendingAccessToken;
    static std::string s_pendingPlayerUuid;
    static std::string s_pendingTokenType;
    static std::string s_username;
    bool m_blockRegistryLoaded = false;
};