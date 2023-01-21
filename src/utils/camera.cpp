#include "camera.h"

#include <cmath>
#include <sstream>
#include <glm/gtc/matrix_transform.hpp>

Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch) :
    front(glm::vec3(0.0f, 0.0f, -1.0f)), movementSpeed(SPEED), mouseSentivity(SENSITIVITY),
    zoom(ZOOM) {
        position = position;
        worldUp = up;
        m_yaw = yaw;
        m_pitch = pitch;
        updateCameraVectors();
    }

Camera::Camera(float posX, float posY, float posZ, float upX, float upY, float upZ, float yaw, float pitch) :
    front(glm::vec3(0.0f, 0.0f, -1.0f)), movementSpeed(SPEED), mouseSentivity(SENSITIVITY),
    zoom(ZOOM) {
        position = glm::vec3(posX, posY, posZ);
        worldUp = glm::vec3(upX, upY, upZ);
        m_yaw = yaw;
        m_pitch = pitch;
        updateCameraVectors();
    }

void Camera::updateCameraVectors() {
    glm::vec3 newFront;
    newFront.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    newFront.y = sin(glm::radians(m_pitch));
    newFront.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
    front = newFront;

    right = glm::normalize(glm::cross(front, worldUp));
    up = glm::normalize(glm::cross(right, front));
}

glm::mat4 Camera::getViewMatrix() {
    return glm::lookAt(position, position + front, up);
}

std::string Camera::getViewDebug() {
    glm::mat4 viewMatrix = getViewMatrix();
    std::stringstream stream;

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            stream << viewMatrix[x][y];
            if (x == 3) stream << "\n";
            else stream << " ";
        }
    }

    return stream.str();
}

void Camera::processKeyboard(Camera_Movement direction, float deltaTime) {
    float velocity = movementSpeed * deltaTime;

    if (direction == FORWARD)
        position += front * velocity;
    if (direction == BACKWARD)
        position -= front * velocity;
    if (direction == LEFT)
        position -= right * velocity;
    if (direction == RIGHT)
        position += right * velocity;
}
void Camera::processMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    xoffset *= mouseSentivity;
    yoffset *= mouseSentivity;

    m_yaw += xoffset;
    m_pitch += yoffset;

    if (constrainPitch) {
        if (m_pitch > 89.0f)
            m_pitch = 89.0f;
        if (m_pitch < -89.0f)
            m_pitch = -89.0f;
    }

    updateCameraVectors();
}

void Camera::processMouseScroll(float yoffset) {
    zoom -= yoffset;

    if (zoom < 1.0f)
        zoom = 1.0f;
    if (zoom > 45.0f)
        zoom = 45.0f;
}