#pragma once
#include <glm/glm.hpp>
#include <mutex>

class Camera {
public:
    static Camera& getInstance() {
        static Camera instance;
        return instance;
    }

    glm::vec3 getPosition() const;
    glm::vec3 getSmoothPosition() const;
    float getPitch() const;
    float getYaw() const;

    void setPosition(float x, float y, float z);
    void setRotation(float pitch, float yaw);
    void updateRotation(float pitchDelta, float yawDelta);

    // 图形 API 无关的矩阵构建（纯数学，GL/Vulkan 共用）
    // 视图矩阵：传入脚部坐标，内部加眼睛高度 1.62 并限制俯仰角 ±89°
    static glm::mat4 computeViewMatrix(float x, float y, float z, float pitch, float yaw);
    static glm::mat4 computeProjectionMatrix(float fovDegrees, float aspect, float nearPlane, float farPlane);

private:
    Camera();

    float pitch;
    float yaw;
    mutable std::mutex mutex;
};
