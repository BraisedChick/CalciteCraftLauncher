#include "CameraController.h"
#include <glm/gtc/matrix_transform.hpp>
#include <android/log.h>
#include <cmath>

#define LOG_TAG "CameraController"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 按键码定义
#define KEY_W 0
#define KEY_S 1
#define KEY_A 2
#define KEY_D 3
#define KEY_UP 4
#define KEY_DOWN 5

CameraController::CameraController() 
    : position(-176.0f, 65.0f, -56.0f), pitch(-0.3f), yaw(3.14159f) {
    LOGI("CameraController initialized at (%.2f, %.2f, %.2f)", 
         position.x, position.y, position.z);
}

void CameraController::setKeyState(int key, bool pressed) {
    std::lock_guard<std::mutex> lock(mutex);
    
    switch (key) {
        case KEY_W: keyW = pressed; break;
        case KEY_S: keyS = pressed; break;
        case KEY_A: keyA = pressed; break;
        case KEY_D: keyD = pressed; break;
        case KEY_UP: keyUp = pressed; break;
        case KEY_DOWN: keyDown = pressed; break;
    }
}

void CameraController::setJoystickInput(float dx, float dy) {
    std::lock_guard<std::mutex> lock(mutex);
    joystickDX = dx;
    joystickDY = dy;
}

void CameraController::update(float deltaTime) {
    std::lock_guard<std::mutex> lock(mutex);
    processMovement(deltaTime);
}

void CameraController::processMovement(float deltaTime) {
    // 计算前向量和右向量（使用 Botcraft 算法）
    float cosYaw = cosf(yaw);
    float sinYaw = sinf(yaw);
    float cosPitch = cosf(pitch);
    float sinPitch = sinf(pitch);
    
    // 前向量
    glm::vec3 front;
    front.x = -sinYaw * cosPitch;
    front.y = -sinPitch;
    front.z = cosYaw * cosPitch;
    front = glm::normalize(front);
    
    // 水平前向量（忽略 Y 轴）
    glm::vec3 horizontalFront(front.x, 0.0f, front.z);
    if (glm::length(horizontalFront) > 1e-6f) {
        horizontalFront = glm::normalize(horizontalFront);
    }
    
    // 右向量
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
    
    // 计算移动方向
    glm::vec3 moveDir(0.0f, 0.0f, 0.0f);
    
    // 键盘控制
    if (keyW) moveDir += horizontalFront;
    if (keyS) moveDir -= horizontalFront;
    if (keyA) moveDir -= right;
    if (keyD) moveDir += right;
    
    // 摇杆控制
    if (fabs(joystickDY) > 0.1f || fabs(joystickDX) > 0.1f) {
        // joystickDY: 负值向前，正值向后
        if (joystickDY < -0.1f) {
            moveDir += horizontalFront * (-joystickDY);
        } else if (joystickDY > 0.1f) {
            moveDir -= horizontalFront * joystickDY;
        }
        
        // joystickDX: 负值向左，正值向右
        if (joystickDX < -0.1f) {
            moveDir -= right * (-joystickDX);
        } else if (joystickDX > 0.1f) {
            moveDir += right * joystickDX;
        }
    }
    
    // 应用水平移动
    if (glm::length(moveDir) > 0.001f) {
        position += moveDir * moveSpeed * deltaTime;
    }
    
    // 垂直移动
    if (keyUp) position.y += moveSpeed * deltaTime;
    if (keyDown) position.y -= moveSpeed * deltaTime;
}

glm::vec3 CameraController::getPosition() const {
    std::lock_guard<std::mutex> lock(mutex);
    return position;
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
    std::lock_guard<std::mutex> lock(mutex);
    position = glm::vec3(x, y, z);
    LOGI("Camera position set to (%.2f, %.2f, %.2f)", x, y, z);
}

void CameraController::setRotation(float newPitch, float newYaw) {
    std::lock_guard<std::mutex> lock(mutex);
    
    // 限制俯仰角范围 (-89° 到 +89°)
    const float maxPitch = glm::radians(89.0f);
    pitch = glm::clamp(newPitch, -maxPitch, maxPitch);
    yaw = newYaw;
    
    // 规范化 yaw 到 [0, 2π)
    if (yaw < 0) yaw += 2.0f * glm::pi<float>();
    if (yaw >= 2.0f * glm::pi<float>()) yaw -= 2.0f * glm::pi<float>();
}
