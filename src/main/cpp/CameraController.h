#pragma once
#include <glm/glm.hpp>
#include <mutex>

class CameraController {
public:
    static CameraController& getInstance() {
        static CameraController instance;
        return instance;
    }

    glm::vec3 getPosition() const;
    float getPitch() const;
    float getYaw() const;

    void setPosition(float x, float y, float z);
    void setRotation(float pitch, float yaw);
    void updateRotation(float pitchDelta, float yawDelta);

private:
    CameraController();

    float pitch;
    float yaw;
    mutable std::mutex mutex;
};
