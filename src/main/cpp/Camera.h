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

private:
    Camera();

    float pitch;
    float yaw;
    mutable std::mutex mutex;
};
