#include "ClientEngine.h"
#include "GameEngine.h"
#include "GLRenderer.h"
#include "EntityRenderer.h"
#include "utils.h"

ClientEngine* ClientEngine::instance = nullptr;
std::string ClientEngine::s_pendingAccessToken;
std::string ClientEngine::s_pendingPlayerUuid;
std::string ClientEngine::s_pendingTokenType;
std::string ClientEngine::s_username = "Player";

ClientEngine::ClientEngine() {
    instance = this;
    m_entityRenderer = std::make_unique<EntityRenderer>();
}

ClientEngine::~ClientEngine() {
    m_gameEngine.reset();  // 先销毁会话
    m_entityRenderer->clearTextureCache();  // 清理实体渲染器 GL 资源
    instance = nullptr;
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
