#include "CameraController.h"
#include "Collision.h"
#include <glm/gtc/matrix_transform.hpp>
#include <android/log.h>

#define LOG_TAG "CameraController"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

CameraController::CameraController()
    : pitch(-0.3f), yaw(3.14159f) {
    LOGI("CameraController initialized");
}

glm::vec3 CameraController::getPosition() const {
    return Collision::getInstance().getPosition();
}

float CameraController::getPitch() const {
    std::lock_guard<std::mutex> lock(mutex);
    return pitch;
}

float CameraController::getYaw() const {
    std::lock_guard<std::mutex> lock(mutex);
    return yaw;
}

void CameraController::setPosition(float x, float y, float z) {
    Collision::getInstance().setPosition(x, y, z);
    LOGI("Camera position set to (%.2f, %.2f, %.2f)", x, y, z);
}

void CameraController::setRotation(float newPitch, float newYaw) {
    std::lock_guard<std::mutex> lock(mutex);
    const float maxPitch = glm::radians(89.0f);
    pitch = glm::clamp(newPitch, -maxPitch, maxPitch);
    yaw = newYaw;
    if (yaw < 0) yaw += 2.0f * glm::pi<float>();
    if (yaw >= 2.0f * glm::pi<float>()) yaw -= 2.0f * glm::pi<float>();
}

void CameraController::updateRotation(float pitchDelta, float yawDelta) {
    std::lock_guard<std::mutex> lock(mutex);
    pitch += pitchDelta;
    yaw += yawDelta;
    const float maxPitch = glm::radians(89.0f);
    pitch = glm::clamp(pitch, -maxPitch, maxPitch);
    if (yaw < 0) yaw += 2.0f * glm::pi<float>();
    if (yaw >= 2.0f * glm::pi<float>()) yaw -= 2.0f * glm::pi<float>();
}
