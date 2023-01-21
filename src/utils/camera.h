#pragma once

#include <glm/glm.hpp>
#include <string>

enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT
};

struct CameraInfo {
    float lastX = 1280.0f / 2.0;
    float lastY = 720.0f / 2.0;
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    bool firstMouse = true;
};

const float YAW = -90.0f, PITCH = 0.0f, SPEED = 2.5f;
const float SENSITIVITY = 0.1f;
const float ZOOM = 45.0f;

class Camera {
    public:
        glm::vec3 position;
        glm::vec3 front;
        glm::vec3 up;
        glm::vec3 right;
        glm::vec3 worldUp;

        float m_yaw;
        float m_pitch;

        float movementSpeed;
        float mouseSentivity;
        float zoom;

        Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
            float yaw = YAW, float pitch = PITCH);
        Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch);

        glm::mat4 getViewMatrix();
        std::string getViewDebug();

        void processKeyboard(Camera_Movement direction, float deltaTime);
        void processMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
        void processMouseScroll(float yoffset);

    private:
        void updateCameraVectors();
};