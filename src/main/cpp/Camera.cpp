#include "Camera.h"
#include "Collision.h"
#include "ClientEngine/ClientEngine.h"
#include "ClientEngine/GameEngine.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <android/log.h>

#define LOG_TAG "Camera"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

Camera::Camera()
    : pitch(-0.3f), yaw(3.14159f) {
    LOGI("Camera initialized");
}

glm::vec3 Camera::getPosition() const {
    auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!game || !game->getCollision()) return glm::vec3(0.0f, 60.0f, 0.0f);
    return game->getCollision()->getPosition();
}

glm::vec3 Camera::getSmoothPosition() const {
    auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (!game || !game->getCollision()) return glm::vec3(0.0f, 60.0f, 0.0f);
    return game->getCollision()->getSmoothPosition();
}

float Camera::getPitch() const {
    std::lock_guard<std::mutex> lock(mutex);
    return pitch;
}

float Camera::getYaw() const {
    std::lock_guard<std::mutex> lock(mutex);
    return yaw;
}

void Camera::setPosition(float x, float y, float z) {
    auto* game = ClientEngine::getInstance() ? ClientEngine::getInstance()->getGame() : nullptr;
    if (game && game->getCollision()) game->getCollision()->setPosition(x, y, z);
    LOGI("Camera position set to (%.2f, %.2f, %.2f)", x, y, z);
}

void Camera::setRotation(float newPitch, float newYaw) {
    std::lock_guard<std::mutex> lock(mutex);
    const float maxPitch = glm::radians(89.0f);
    pitch = glm::clamp(newPitch, -maxPitch, maxPitch);
    yaw = newYaw;
    if (yaw < 0) yaw += 2.0f * glm::pi<float>();
    if (yaw >= 2.0f * glm::pi<float>()) yaw -= 2.0f * glm::pi<float>();
}

void Camera::updateRotation(float pitchDelta, float yawDelta) {
    std::lock_guard<std::mutex> lock(mutex);
    pitch += pitchDelta;
    yaw += yawDelta;
    const float maxPitch = glm::radians(89.0f);
    pitch = glm::clamp(pitch, -maxPitch, maxPitch);
    if (yaw < 0) yaw += 2.0f * glm::pi<float>();
    if (yaw >= 2.0f * glm::pi<float>()) yaw -= 2.0f * glm::pi<float>();
}

glm::mat4 Camera::computeViewMatrix(float x, float y, float z, float pitch, float yaw) {
    // 加眼睛高度偏移（玩家脚部 → 眼睛，原版 1.62 格）
    y += 1.62f;

    // 限制俯仰角
    const float maxPitch = glm::radians(89.0f);
    pitch = glm::clamp(pitch, -maxPitch, maxPitch);

    // 前方向向量
    glm::vec3 front;
    front.x = -sinf(yaw) * cosf(pitch);
    front.y = -sinf(pitch);
    front.z = cosf(yaw) * cosf(pitch);
    front = glm::normalize(front);

    // 右向量和上向量
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
    glm::vec3 up = glm::normalize(glm::cross(right, front));

    glm::vec3 position(x, y, z);
    return glm::lookAt(position, position + front, up);
}

glm::mat4 Camera::computeProjectionMatrix(float fovDegrees, float aspect, float nearPlane, float farPlane) {
    return glm::perspective(glm::radians(fovDegrees), aspect, nearPlane, farPlane);
}
