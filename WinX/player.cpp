// ============================================================================
// player.cpp - First-Person Movement Controller (GoldSrc-style)
// ============================================================================
//
// WHAT THIS FILE IS
// ----------------------------------------------------------------------------
// Turns keyboard input into player motion with the classic Half-Life "GoldSrc"
// feel: quick acceleration on the ground, floaty air control, friction that
// stops you fast, a jump buffer, a coyote timer, and smooth crouching. Actual
// collision and gravity are handled by Jolt (jolt_world.cpp) - this file only
// decides what velocity the player WANTS, then hands it to Jolt.
//
// HOW TO UNDERSTAND IT
// ----------------------------------------------------------------------------
// - update() is called every frame (only while the mouse is locked). It:
//   1. Builds forward/right vectors from the camera's yaw (ignoring pitch so
//      you can't fly by looking up).
//   2. Sums WASD into a "wish direction" (wishDir) scaled by walk/sprint/crouch.
//   3. Jump logic: m_jumpBufferTimer ("I pressed jump recently") and
//      m_lastGroundedTime/coyote ("I was on the ground recently") - the two
//      together make jumping feel responsive even when timed slightly early or
//      late. This is the modern take on GoldSrc's jump.
//   4. Chooses ground acceleration (applyFriction + accelerate) or air control
//      (airAccelerate). These three functions ARE the GoldSrc movement feel.
//   5. Hands the desired velocity to joltWorld.step(), which integrates gravity,
//      collides the capsule with the world, and returns the resolved position/
//      velocity/grounded state.
//   6. Smoothly interpolates eye height when crouching (duck feel).
//   7. Sets camera.position to the eyes (feet + eye height).
//
// THE THREE MOVEMENT MATH FUNCTIONS (study these)
// ----------------------------------------------------------------------------
// - applyFriction(): cut horizontal speed by a fixed drop each second. Below
//   m_Stopspeed it scales so you fully stop.
// - accelerate(): add to velocity along the wish direction, capped per frame.
// - airAccelerate(): same but capped by m_AirSpeedCap (slower, floatier).
// These are copied from the GoldSrc/Half-Life SDK and are what make the movement
// feel "right" rather than generic.
//
// KEY IDEAS
// ----------------------------------------------------------------------------
// - deltaTime is clamped to 0.05s so a stutter frame doesn't teleport you.
// - The split of responsibilities matters: Player picks velocity, Jolt does the
//   physics. Change movement feel here; change collision there.
// ============================================================================
#include "player.h"
#include "jolt_world.h"
#include <cmath>

void Player::update(GLFWwindow* window, float deltaTime, Camera& camera, JoltWorld& joltWorld) {
    deltaTime = glm::min(deltaTime, 0.05f);
    m_time += deltaTime;

    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

    glm::vec3 forward = glm::normalize(glm::vec3(camera.front.x, 0.0f, camera.front.z));
    if (glm::length(forward) < 1e-5f)
        forward = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));

    glm::vec3 wishDir(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wishDir += forward;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wishDir -= forward;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wishDir += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wishDir -= right;
    if (glm::dot(wishDir, wishDir) > 1e-6f) wishDir = glm::normalize(wishDir);

    bool sprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
    bool crouchKey = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS
        || glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS;
    m_IsCrouching = crouchKey;

    float wishSpeed = sprint ? m_SprintSpeed : m_WalkSpeed;
    if (m_IsCrouching) wishSpeed *= m_CrouchSpeedMul;

    bool wasGrounded = m_IsGrounded;

    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        m_jumpBufferTimer = m_JumpBufferTime;
    else
        m_jumpBufferTimer = glm::max(0.0f, m_jumpBufferTimer - deltaTime);

    if (m_IsGrounded) m_lastGroundedTime = m_time;
    bool canJump = (m_time - m_lastGroundedTime) <= m_CoyoteTime;
    if (m_jumpBufferTimer > 0.0f && canJump) {
        m_Velocity.y = m_JumpSpeed;
        m_jumpBufferTimer = 0.0f;
        m_lastGroundedTime = -1000.0f;
        m_IsGrounded = false;
    }

    if (wasGrounded) {
        applyFriction(deltaTime);
        accelerate(wishDir, wishSpeed, m_Accelerate, deltaTime);
    }
    else {
        airAccelerate(wishDir, wishSpeed, m_AirAccelerate, deltaTime);
    }

    // Jolt owns all movement and collision: it integrates gravity, resolves the capsule
    // against the world (walls, slopes, stairs, falling, landing) and writes back the
    // resolved position, velocity and grounded state. This controller only picks the
    // desired velocity, so the GoldSrc acceleration/friction feel is preserved.
    joltWorld.step(deltaTime, m_Position, m_Velocity, m_IsGrounded);

    // Smooth eye height so crouching feels like Half-Life's duck.
    float targetEye = m_IsCrouching ? m_CrouchEyeHeight : m_EyeHeight;
    m_eyeHeightCurrent += (targetEye - m_eyeHeightCurrent) * (1.0f - expf(-12.0f * deltaTime));

    camera.position = m_Position + worldUp * m_eyeHeightCurrent;
}

void Player::applyFriction(float deltaTime) {
    glm::vec3 horizontal = m_Velocity;
    horizontal.y = 0.0f;

    float speed = glm::length(horizontal);
    if (speed < 0.001f) return;

    float control = speed < m_Stopspeed ? m_Stopspeed : speed;
    float drop = control * m_Friction * deltaTime;

    float newSpeed = speed - drop;
    if (newSpeed < 0.0f) newSpeed = 0.0f;

    float factor = newSpeed / speed;
    m_Velocity.x *= factor;
    m_Velocity.z *= factor;
}

void Player::accelerate(const glm::vec3& wishDir, float wishSpeed, float accel, float deltaTime) {
    float currentSpeed = glm::dot(m_Velocity, wishDir);
    float addSpeed = wishSpeed - currentSpeed;
    if (addSpeed <= 0.0f) return;

    float accelSpeed = accel * deltaTime * wishSpeed;
    if (accelSpeed > addSpeed) accelSpeed = addSpeed;

    m_Velocity += wishDir * accelSpeed;
}

void Player::airAccelerate(const glm::vec3& wishDir, float wishSpeed, float accel, float deltaTime) {
    float wishSpd = wishSpeed;
    if (wishSpd > m_AirSpeedCap) wishSpd = m_AirSpeedCap;

    float currentSpeed = glm::dot(m_Velocity, wishDir);
    float addSpeed = wishSpd - currentSpeed;
    if (addSpeed <= 0.0f) return;

    float accelSpeed = accel * deltaTime * wishSpeed;
    if (accelSpeed > addSpeed) accelSpeed = addSpeed;

    m_Velocity += wishDir * accelSpeed;
}
