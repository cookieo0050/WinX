#pragma once
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include "camera.h"
#include "collision.h"

class Player {
public:
    glm::vec3 m_Position = glm::vec3(0.0f);
    glm::vec3 m_Velocity = glm::vec3(0.0f);
    bool m_IsGrounded = false;

    float m_EyeHeight = 1.5f;
    float m_Radius = 0.35f;
    float m_StepHeight = 0.7f;

    float m_WalkSpeed = 4.0f;
    float m_SprintSpeed = 7.0f;
    float m_Accelerate = 10.0f;
    float m_AirAccelerate = 10.0f;
    float m_Friction = 4.0f;
    float m_Stopspeed = 0.8f;
    float m_AirSpeedCap = 12.0f;
    float m_JumpSpeed = 7.5f;
    float m_MaxFallSpeed = 20.0f;
    float m_Gravity = 22.0f;
    float m_CoyoteTime = 0.12f;
    float m_JumpBufferTime = 0.12f;

    void update(GLFWwindow* window, const CollisionMesh& collisionMesh, float deltaTime, Camera& camera);

private:
    CollisionContact integrateStep(const CollisionMesh& collisionMesh, float deltaTime);
    bool tryStepUp(const CollisionMesh& collisionMesh, const glm::vec3& startPos, const glm::vec3& horizDisp);
    void applyFriction(float deltaTime);
    void accelerate(const glm::vec3& wishDir, float wishSpeed, float accel, float deltaTime);
    void airAccelerate(const glm::vec3& wishDir, float wishSpeed, float accel, float deltaTime);

    float m_time = 0.0f;
    float m_lastGroundedTime = -1000.0f;
    float m_jumpBufferTimer = 0.0f;
};
