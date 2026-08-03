#pragma once
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include "camera.h"

class JoltWorld;

class Player {
public:
    glm::vec3 m_Position = glm::vec3(0.0f);
    glm::vec3 m_Velocity = glm::vec3(0.0f);
    bool m_IsGrounded = false;

    float m_EyeHeight = 1.5f;

    // GoldSrc / Half-Life 1 style tuning (metres, 1 unit ~= 1 inch)
    float m_WalkSpeed = 4.0f;
    float m_SprintSpeed = 7.8f;
    float m_Accelerate = 4.0f;
    float m_AirAccelerate = 0.8f;
    float m_Friction = 3.0f;
    float m_Stopspeed = 2.4f;
    float m_AirSpeedCap = 8.0f;
    float m_JumpSpeed = 6.6f;
    float m_CoyoteTime = 0.04f;
    float m_JumpBufferTime = 0.04f;

    // Slopes with normal.y >= this can be walked up; steeper acts as a wall.
    float m_SlopeClimbNormalY = 0.4f;

    // Crouch / duck
    float m_CrouchEyeHeight = 0.65f;
    float m_CrouchSpeedMul = 0.5f;

    void update(GLFWwindow* window, float deltaTime, Camera& camera, JoltWorld& joltWorld);

    bool isCrouching() const { return m_IsCrouching; }
    float eyeHeight() const { return m_eyeHeightCurrent; }

private:
    void applyFriction(float deltaTime);
    void accelerate(const glm::vec3& wishDir, float wishSpeed, float accel, float deltaTime);
    void airAccelerate(const glm::vec3& wishDir, float wishSpeed, float accel, float deltaTime);

    float m_time = 0.0f;
    float m_lastGroundedTime = -1000.0f;
    float m_jumpBufferTimer = 0.0f;
    float m_eyeHeightCurrent = 1.5f;
    bool m_IsCrouching = false;
};
