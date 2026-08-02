#include "player.h"
#include <cmath>

void Player::update(GLFWwindow* window, const CollisionMesh& collisionMesh, float deltaTime, Camera& camera) {
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

    // GoldSrc gravity: constant, no variable jump height.
    m_Velocity.y -= m_Gravity * deltaTime;

    m_Velocity.y = glm::max(m_Velocity.y, -m_MaxFallSpeed);

    float maxStep = m_Radius * 0.35f;
    float moveDistance = glm::length(m_Velocity) * deltaTime;
    int substeps = moveDistance > maxStep ? (int)glm::ceil(moveDistance / maxStep) : 1;
    float stepDt = deltaTime / (float)substeps;

    CollisionContact lastContact;
    for (int s = 0; s < substeps; ++s)
        lastContact = integrateStep(collisionMesh, stepDt);

    m_IsGrounded = false;
    if (lastContact.collided && glm::dot(lastContact.normal, worldUp) > 0.6f) {
        m_IsGrounded = true;
        if (m_Velocity.y <= 0.0f) m_Velocity.y = 0.0f;
    }
    else {
        RaycastHit ground = collisionMesh.raycast(m_Position, glm::vec3(0.0f, -1.0f, 0.0f), m_Radius + 0.15f);
        if (ground.hit && m_Velocity.y <= 0.0f) {
            m_IsGrounded = true;
            m_Position.y = ground.point.y + m_Radius;
            m_Velocity.y = 0.0f;
        }
    }

    // Smooth eye height so crouching feels like Half-Life's duck.
    float targetEye = m_IsCrouching ? m_CrouchEyeHeight : m_EyeHeight;
    m_eyeHeightCurrent += (targetEye - m_eyeHeightCurrent) * (1.0f - expf(-12.0f * deltaTime));

    camera.position = m_Position + worldUp * m_eyeHeightCurrent;
}

CollisionContact Player::integrateStep(const CollisionMesh& mesh, float dt) {
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);

    glm::vec3 horizDisp = m_Velocity;
    horizDisp.y = 0.0f;
    horizDisp *= dt;

    glm::vec3 tryPos = m_Position + horizDisp;
    CollisionContact cH = mesh.resolveSphereCollisionDetailed(tryPos, m_Radius);

    // Follow climbable slopes and slide along walls instead of stopping dead,
    // which keeps momentum through ramps and acute-angle geometry.
    if (cH.collided) {
        float contactUp = glm::dot(cH.normal, worldUp);
        float intoSurface = glm::dot(horizDisp, cH.normal);

        if (contactUp >= m_SlopeClimbNormalY) {
            // Climbable slope: move along the surface plane (gains an upward component).
            if (intoSurface < 0.0f) {
                glm::vec3 slide = horizDisp - cH.normal * intoSurface;
                if (glm::dot(slide, slide) > 1e-8f) {
                    glm::vec3 slopePos = m_Position + slide;
                    CollisionContact cS = mesh.resolveSphereCollisionDetailed(slopePos, m_Radius);
                    if (!cS.collided || glm::dot(cS.normal, worldUp) >= m_SlopeClimbNormalY) {
                        tryPos = slopePos;
                        cH = cS;
                    }
                }
            }
        }
        else {
            // Wall: slide sideways along the face.
            glm::vec3 slide = horizDisp - cH.normal * intoSurface;
            slide.y = 0.0f;
            if (glm::dot(slide, slide) > 1e-8f) {
                glm::vec3 wallPos = m_Position + slide;
                CollisionContact cW = mesh.resolveSphereCollisionDetailed(wallPos, m_Radius);
                if (!cW.collided) {
                    tryPos = wallPos;
                    cH = cW;
                }
            }
        }
    }

    bool wallBlocked = cH.collided && glm::dot(cH.normal, worldUp) < 0.3f;
    bool canStep = m_StepHeight > 0.0f && m_Velocity.y <= 0.1f;

    if (wallBlocked && canStep && tryStepUp(mesh, m_Position, horizDisp)) {
        // Stepped up onto the obstacle; horizontal velocity is preserved.
    }
    else {
        m_Position = tryPos;
        if (cH.collided) {
            float vn = glm::dot(m_Velocity, cH.normal);
            if (vn < 0.0f) m_Velocity -= cH.normal * vn;
        }
    }

    glm::vec3 vDisp(0.0f, m_Velocity.y * dt, 0.0f);
    m_Position += vDisp;
    CollisionContact cV = mesh.resolveSphereCollisionDetailed(m_Position, m_Radius);
    if (cV.collided) {
        float vn = glm::dot(m_Velocity, cV.normal);
        if (vn < 0.0f) m_Velocity -= cV.normal * vn;
    }

    return cV;
}

bool Player::tryStepUp(const CollisionMesh& mesh, const glm::vec3& startPos, const glm::vec3& horizDisp) {
    const float minLift = 0.1f;
    const float minProgress = 0.005f;
    const glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    const glm::vec3 down(0.0f, -1.0f, 0.0f);

    // Try the full step height first, then smaller lifts if headroom is limited.
    float liftHeight = m_StepHeight;
    for (int attempt = 0; attempt < 4; ++attempt) {
        glm::vec3 upPos = startPos + worldUp * liftHeight;
        CollisionContact cUp = mesh.resolveSphereCollisionDetailed(upPos, m_Radius);
        float lift = upPos.y - startPos.y;
        if (lift >= minLift) {
            glm::vec3 stepPos = upPos + horizDisp;
            CollisionContact cStep = mesh.resolveSphereCollisionDetailed(stepPos, m_Radius);

            glm::vec3 hProgress = stepPos - upPos;
            hProgress.y = 0.0f;
            if (glm::length(hProgress) >= minProgress) {
                glm::vec3 downPos = stepPos;
                RaycastHit ground = mesh.raycast(stepPos, down, liftHeight + m_Radius + 0.5f);
                if (ground.hit)
                    downPos.y = ground.point.y + m_Radius;
                else
                    downPos.y = stepPos.y;

                CollisionContact cFinal = mesh.resolveSphereCollisionDetailed(downPos, m_Radius);
                bool ceilingAbove = cFinal.collided && glm::dot(cFinal.normal, worldUp) < -0.3f;
                if (!ceilingAbove) {
                    m_Position = downPos;
                    return true;
                }
            }
        }
        liftHeight *= 0.5f;
    }
    return false;
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
