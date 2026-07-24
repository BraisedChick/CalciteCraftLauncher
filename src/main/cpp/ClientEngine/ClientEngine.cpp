#include "ClientEngine.h"
#include "GameEngine.h"
#include "GLRenderer.h"
#include "EntityRenderer.h"
#include "TextureAtlas.h"
#include "BlockRegistry.h"
#include "TextureLoader.h"
#include "utils.h"
#include <android/native_window.h>
#include "gui/ScreenManager.h"
#include "gui/TitleScreen.h"
#include <atomic>
#include "MusicManager.h"
extern std::atomic<bool> g_renderThreadIdle;
ClientEngine* ClientEngine::instance = nullptr;
std::string ClientEngine::s_pendingAccessToken;
std::string ClientEngine::s_pendingPlayerUuid;
std::string ClientEngine::s_pendingTokenType;
std::string ClientEngine::s_username = "Player";

ClientEngine::ClientEngine() {
    instance = this;
    m_textureAtlas = std::make_unique<TextureAtlas>();
    m_blockRegistry = std::make_unique<BlockRegistry>();
    m_entityRenderer = std::make_unique<EntityRenderer>();
    m_musicManager = std::make_unique<MusicManager>();
    m_musicManager->init();
}

ClientEngine::~ClientEngine() {
    m_musicManager.reset();
    m_gameEngine.reset();
    m_entityRenderer->clearTextureCache();
    instance = nullptr;
}
void ClientEngine::setupUICallbacks() {
    // 连接回调（游戏内点击“加入服务器”触发）
    GameUI::getInstance().setConnectCallback([](const std::string& ip, int port) {
        LOGI("UI Connect: %s:%d user=%s", ip.c_str(), port, ClientEngine::getUsername().c_str());

        // 在独立线程中执行连接，避免阻塞 UI
        std::thread([ip, port]() {
            auto* client = ClientEngine::getInstance();
            if (!client) return;

            // 销毁旧会话
            client->destroyGame();

            // 创建新会话
            auto* game = client->createGame();

            // 传递正版认证信息
            if (ClientEngine::isPremiumPending()) {
                game->setAuthInfo(
                        ClientEngine::getPendingAccessToken(),
                        ClientEngine::getPendingPlayerUuid(),
                        ClientEngine::getPendingTokenType());
            }

            // 加载语言文件
            std::string langJson = TextureLoader::readTextFromZip("lang/zh_cn.json");
            if (!langJson.empty()) {
                game->loadLanguage(langJson);
            }

            // 应用渲染器设置
            if (client->getRenderer()) {
                auto& ui = GameUI::getInstance();
                client->getRenderer()->setFov(ui.getOptionsFov());
                client->getRenderer()->setRenderDistance(ui.getRenderDistance());
                client->getRenderer()->setMipmapLevel(ui.getMipmapLevel());
                client->getRenderer()->setMaxFps(ui.getMaxFps());
            }

            // 切换状态并开始连接
            GameUI::getInstance().setState(UIState::IN_GAME);
            GameUI::getInstance().clearChatMessages();

            // start() 阻塞直到断开连接
            game->start(ip, port);

            // 断开连接后的清理
            LOGI("Disconnected, returning to title screen");
            auto& ui = GameUI::getInstance();
            ui.setState(UIState::MAIN_MENU);

            // 等待渲染线程进入安全分支（需要访问全局变量，暂时保留）
            // 后续第二步迁移渲染线程时，通过 ClientEngine 方法等待
            while (!g_renderThreadIdle.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            LOGI("Render thread in safe branch, cleaning up game engine");

            // 清空区块缓存
            if (client->getRenderer()) {
                client->getRenderer()->clearChunks();
            }

            // 销毁会话
            client->destroyGame();
            LOGI("Game engine destroyed safely");

            // 重置 UI 状态
            ui.setGameMenuOpen(false);
            ui.setDeathScreenActive(false);
            ui.setOptionsOpen(false);
            ui.setInventoryOpen(false);
            ScreenManager::getInstance().setScreen(std::make_unique<TitleScreen>());

        }).detach();
    });

    // 可以在此设置其他回调（退出、断开连接等），但目前它们已在 GameUI::init() 中设置
}
// ===== 新增：渲染器初始化 =====
bool ClientEngine::initializeRenderer(ANativeWindow* window) {
    if (!window) {
        LOGE("initializeRenderer: window is null");
        return false;
    }

    // 加载 BlockRegistry（必须在渲染器初始化之前）
    loadBlockRegistry();

    // 创建 OpenGL ES 渲染器
    auto glRenderer = std::make_unique<GLRenderer>();
    if (!glRenderer->initialize(window)) {
        LOGE("Failed to initialize OpenGL renderer");
        return false;
    }

    setRenderer(std::move(glRenderer));
    LOGI("OpenGL ES renderer initialized successfully");
    return true;
}

void ClientEngine::setRenderer(std::unique_ptr<GLRenderer> renderer) {
    m_renderer = std::move(renderer);
}

std::unique_ptr<GLRenderer> ClientEngine::releaseRenderer() {
    return std::move(m_renderer);
}

void ClientEngine::setPendingAuth(const std::string& accessToken, const std::string& uuid, const std::string& tokenType) {
    s_pendingAccessToken = accessToken;
    s_pendingPlayerUuid = uuid;
    s_pendingTokenType = tokenType;
}

bool ClientEngine::isPremiumPending() {
    return !s_pendingAccessToken.empty();
}

const std::string& ClientEngine::getPendingAccessToken() { return s_pendingAccessToken; }
const std::string& ClientEngine::getPendingPlayerUuid() { return s_pendingPlayerUuid; }
const std::string& ClientEngine::getPendingTokenType() { return s_pendingTokenType; }

GameEngine* ClientEngine::createGame() {
    m_gameEngine = std::make_unique<GameEngine>(this);
    return m_gameEngine.get();
}

void ClientEngine::destroyGame() {
    m_gameEngine.reset();
}

void ClientEngine::loadBlockRegistry() {
    if (m_blockRegistryLoaded) return;
    m_blockRegistryLoaded = true;

    auto* registry = m_blockRegistry.get();
    if (!registry) return;

    std::string blocksJson = TextureLoader::readTextFromZip("blocks.json");
    if (!blocksJson.empty() && registry->loadFromJson(blocksJson)) {
        LOGI("BlockRegistry loaded successfully: %zu blocks", registry->getBlockCount());
    } else {
        LOGE("Failed to load BlockRegistry, using fallback mapping");
    }

    std::string itemsJson = TextureLoader::readTextFromZip("items.json");
    if (!itemsJson.empty() && registry->loadItems(itemsJson)) {
        LOGI("Items loaded successfully from ZIP");
    } else {
        LOGI("No items.json in ZIP, blocks-only mode");
    }
}