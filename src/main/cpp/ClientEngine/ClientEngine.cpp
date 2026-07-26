#include "ClientEngine.h"
#include "GameEngine.h"
#include "GLRenderer.h"
#include "VulkanRenderer.h"
#include "EntityRenderer.h"
#include "TextureAtlas.h"
#include "BlockRegistry.h"
#include "TextureLoader.h"
#include "utils.h"
#include "CameraController.h"
#include "Collision.h"
#include "Light.h"
#include "JniBridge.h"
#include <android/native_window.h>
#include "gui/GameUI.h"
#include "gui/ScreenManager.h"
#include "gui/TitleScreen.h"
#include <atomic>
#include "MusicManager.h"
ClientEngine* ClientEngine::instance = nullptr;
std::string ClientEngine::s_pendingAccessToken;
std::string ClientEngine::s_pendingPlayerUuid;
std::string ClientEngine::s_pendingTokenType;
std::string ClientEngine::s_username = "Player";
ClientEngine::RendererType ClientEngine::s_rendererType = ClientEngine::RendererType::OpenGL;

ClientEngine::ClientEngine() {
    instance = this;
    m_textureAtlas = std::make_unique<TextureAtlas>();
    m_blockRegistry = std::make_unique<BlockRegistry>();
    m_entityRenderer = std::make_unique<EntityRenderer>();
    m_musicManager = std::make_unique<MusicManager>();
    m_musicManager->init();
}

ClientEngine::~ClientEngine() {
    // 防御：若渲染线程仍在运行，先停止（幂等，正常流程由 cleanupRenderer 提前调用）
    stopRenderThread();
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

            // 等待渲染线程进入安全分支
            while (!client->isRenderThreadIdle()) {
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

    // Vulkan 路径（第一步：仅渲染主界面 ImGui）
    if (s_rendererType == RendererType::Vulkan) {
        int w = ANativeWindow_getWidth(window);
        int h = ANativeWindow_getHeight(window);
        auto vkRenderer = std::make_unique<VulkanRenderer>();
        if (!vkRenderer->initialize(window, w, h)) {
            LOGE("Failed to initialize Vulkan renderer");
            return false;
        }
        if (!vkRenderer->initImGui()) {
            LOGE("Failed to initialize ImGui Vulkan backend");
        }
        m_vulkanRenderer = std::move(vkRenderer);
        LOGI("Vulkan renderer initialized: %dx%d", w, h);
        return true;
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

// ===== 渲染线程 =====
void ClientEngine::startRenderThread() {
    if (m_rendering.load()) return;
    LOGI("Starting render thread");
    m_rendering = true;
    m_renderThread = std::thread(&ClientEngine::renderLoop, this);
    // 给渲染线程留出绑定 EGL context 的时间
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void ClientEngine::stopRenderThread() {
    if (m_rendering.exchange(false)) {
        if (m_renderThread.joinable()) {
            LOGI("Waiting for render thread to finish");
            m_renderThread.join();
            LOGI("Render thread joined");
        }
    }
}

void ClientEngine::renderLoop() {
    LOGI("Render thread started");

    // 在渲染线程中重新绑定 EGL context
    if (getRenderer()) {
        getRenderer()->makeCurrent();
    }

    int frameCount = 0;
    auto lastTime = std::chrono::high_resolution_clock::now();

    while (m_rendering) {
        frameCount++;

        // 计算 delta time
        auto currentTime = std::chrono::high_resolution_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        bool inGame = (GameUI::getInstance().getState() == UIState::IN_GAME);

        if (inGame) {
            m_renderThreadIdle.store(false, std::memory_order_release);

            // 确保 PlayerController 和 Light 持有 ChunkManager 引用
            auto* game = getGame();
            if (game) {
                auto* cm = game->getChunkManager();
                if (cm) {
                    if (!game->getCollision()->hasChunkManager()) {
                        game->getCollision()->setChunkManager(cm);
                        LOGI("PlayerController: ChunkManager acquired from engine");
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
                LOGI("Rendering frame %d", frameCount);
            }

            // 从 CameraController 获取摄像机数据
            auto pos = CameraController::getInstance().getSmoothPosition();
            float pitch = CameraController::getInstance().getPitch();
            float yaw = CameraController::getInstance().getYaw();

            if (auto* renderer = getRenderer()) {
                // 取走光照更新波及的脏 chunk，精准 remesh（替代旧版 5×5 无差别重建）
                {
                    static std::vector<std::pair<int, int>> dirtyLightChunks;
                    if (game->getLight()->pollDirtyLightChunks(dirtyLightChunks)) {
                        for (const auto& [cx, cz] : dirtyLightChunks) {
                            renderer->markChunkForUpdate(cx, cz);
                        }
                    }
                }

                renderer->render(pos.x, pos.y, pos.z, pitch, yaw);
            } else if (auto* vkRenderer = getVulkanRenderer()) {
                vkRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
            }
        } else {
            // 非游戏状态：不访问任何引擎资源，GLRenderer::render() 内部渲染全景+ImGui
            m_renderThreadIdle.store(true, std::memory_order_release);

            auto pos = CameraController::getInstance().getSmoothPosition();
            float pitch = CameraController::getInstance().getPitch();
            float yaw = CameraController::getInstance().getYaw();

            if (auto* activeRenderer = getRenderer()) {
                activeRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
            } else if (auto* vkRenderer = getVulkanRenderer()) {
                vkRenderer->render(pos.x, pos.y, pos.z, pitch, yaw);
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
            auto* music = getMusicManager();
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