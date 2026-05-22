#pragma once
#include <glm/glm.hpp>
#include <mutex>

class CameraController {
public:
    static CameraController& getInstance() {
        static CameraController instance;
        return instance;
    }

    // 更新输入状态（从 Java 层调用，只在按键变化时调用）
    void setKeyState(int key, bool pressed);
    void setJoystickInput(float dx, float dy);
    
    // 更新摄像机（在渲染循环中调用，每帧执行）
    void update(float deltaTime);
    
    // 获取摄像机状态（供渲染器使用）
    glm::vec3 getPosition() const;
    float getPitch() const;
    float getYaw() const;
    
    // 设置初始位置
    void setPosition(float x, float y, float z);
    void setRotation(float pitch, float yaw);
    
    // 更新旋转（相对变化量）
    void updateRotation(float pitchDelta, float yawDelta);

private:
    CameraController();
    
    // 摄像机状态
    glm::vec3 position;
    float pitch;
    float yaw;
    
    // 输入状态
    bool keyW = false;
    bool keyS = false;
    bool keyA = false;
    bool keyD = false;
    bool keyUp = false;
    bool keyDown = false;
    float joystickDX = 0.0f;
    float joystickDY = 0.0f;
    
    // 移动参数
    float moveSpeed = 5.0f;  // 单位/秒
    
    // 线程安全
    mutable std::mutex mutex;
    
    // 内部方法
    void processMovement(float deltaTime);
};
