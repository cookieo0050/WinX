// ============================================================================
// jolt_world.cpp - Jolt Physics World & Virtual Character
// ============================================================================
//
// WHAT THIS FILE IS
// ----------------------------------------------------------------------------
// Wraps the Jolt Physics library (JoltPhysics-master/) into a single class that
// owns: the static world body (built from the level's collision triangles), a
// "virtual character" capsule for the player, and per-frame stepping. The player
// controller (player.cpp) picks the desired velocity; this file applies gravity,
// moves the capsule, resolves collisions against the world and hands back the
// final position/velocity/grounded state.
//
// HOW TO UNDERSTAND IT
// ----------------------------------------------------------------------------
// - The nested structs at the top (BPLayerInterface, ObjVsBpFilter,
//   ObjPairFilter) tell Jolt which object layers collide with each other
//   (NON_MOVING world vs MOVING character). They are boilerplate you can mostly
//   ignore unless you add new layers.
// - init(): the "big setup". It boots the whole Jolt library (factory, types,
//   allocator), copies the collision triangles into a JPH::MeshShape as a static
//   body, then creates the CharacterVirtual - a capsule 1.8m tall, 0.35m radius,
//   with maxSlopeAngle = 35 degrees (tuned so walls can't be climbed).
// - step(): per frame. It:
//   1. Applies gravity to the desired velocity (capped at m_MaxFallSpeed).
//   2. Calls m_Character->ExtendedUpdate - Jolt's character controller that does
//      gravity + collision + ground detection in one call.
//   3. Reads back position/velocity/grounded. grounded uses GetGroundState()==
//      OnGround (not IsSupported) so leaning on a wall doesn't count as standing.
//   4. If grounded but the frame barely moved horizontally, tries tryStepUp()
//      for deterministic stair climbing.
// - tryStepUp(): a hand-written stair stepper. It raycasts (with the custom
//   collision.cpp mesh) to check 4 things: headroom after lifting, forward
//   clearance, that the blocker is a steep riser (not a ramp), and that there is
//   a real flat tread to land on. Only then does it teleport the feet up.
// - shutdown(): tears everything down in reverse order (character first, then
//   the Jolt factory) - Jolt asserts if you destroy things in the wrong order.
//
// KEY IDEAS
// ----------------------------------------------------------------------------
// - Jolt is a third-party physics engine (the // Jolt/ includes). This file is
//   the ONLY place the engine talks to it - everything else just uses JoltWorld.
// - CharacterVirtual = no actual rigid body, just a moving capsule; perfect for
//   FPS controllers because the game controls its velocity directly.
// - m_Gravity, m_MaxFallSpeed, m_StepHeight, m_CharacterHeight/Radius live in the
//   header - those are the "feel" constants.
// ============================================================================
#include "jolt_world.h"
#include "console.h"
#include <iostream>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>

namespace {

namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING = 1;
    static constexpr JPH::uint NUM_LAYERS = 2;
}

namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr JPH::uint NUM_LAYERS = 2;
}

} // namespace

struct JoltWorld::BPLayerInterface : public JPH::BroadPhaseLayerInterface {
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return inLayer == Layers::NON_MOVING ? BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:     return "MOVING";
        default:                                                       return "INVALID";
        }
    }
#endif
};

struct JoltWorld::ObjVsBpFilter : public JPH::ObjectVsBroadPhaseLayerFilter {
    bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
        case Layers::NON_MOVING: return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING:     return true;
        default: JPH_ASSERT(false); return false;
        }
    }
};

struct JoltWorld::ObjPairFilter : public JPH::ObjectLayerPairFilter {
    bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
        case Layers::NON_MOVING: return inObject2 == Layers::MOVING;
        case Layers::MOVING:     return true;
        default: JPH_ASSERT(false); return false;
        }
    }
};

JoltWorld::JoltWorld() = default;

JoltWorld::~JoltWorld() {
    shutdown();
}

bool JoltWorld::init(const CollisionMesh& collisionMesh, const glm::vec3& playerStart) {
    shutdown();

    m_CollisionMesh = &collisionMesh;

    // Global Jolt setup. RegisterTypes needs a factory, so create it first.
    JPH::RegisterDefaultAllocator();
    m_TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
    if (JPH::Factory::sInstance == nullptr)
        JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    m_BpLayerInterface = std::make_unique<BPLayerInterface>();
    m_ObjVsBpFilter = std::make_unique<ObjVsBpFilter>();
    m_ObjPairFilter = std::make_unique<ObjPairFilter>();

    const JPH::uint maxBodies = 4096;
    const JPH::uint numBodyMutexes = 0;
    const JPH::uint maxBodyPairs = 65536;
    const JPH::uint maxContactConstraints = 10240;
    m_PhysicsSystem.Init(maxBodies, numBodyMutexes, maxBodyPairs, maxContactConstraints,
        *m_BpLayerInterface, *m_ObjVsBpFilter, *m_ObjPairFilter);

    // Static world mesh built from the level collision triangles.
    JPH::VertexList vertices;
    JPH::IndexedTriangleList indices;
    const std::vector<Triangle>& tris = collisionMesh.triangles();
    vertices.reserve(tris.size() * 3);
    indices.reserve(tris.size());
    JPH::uint32 base = 0;
    for (const Triangle& t : tris) {
        vertices.push_back(JPH::Float3(t.v0.x, t.v0.y, t.v0.z));
        vertices.push_back(JPH::Float3(t.v1.x, t.v1.y, t.v1.z));
        vertices.push_back(JPH::Float3(t.v2.x, t.v2.y, t.v2.z));
        indices.push_back(JPH::IndexedTriangle(base, base + 1, base + 2));
        base += 3;
    }
    if (vertices.empty()) {
        std::cerr << "[JoltWorld] No collision geometry to build physics world\n";
        g_Console.logError("No collision geometry to build physics world");
        return false;
    }

    JPH::MeshShapeSettings meshSettings(vertices, indices);
    meshSettings.SetEmbedded();
    JPH::ShapeSettings::ShapeResult meshResult = meshSettings.Create();
    if (meshResult.HasError()) {
        std::cerr << "[JoltWorld] MeshShape error: " << meshResult.GetError() << "\n";
        g_Console.logError("MeshShape error: " + std::string(meshResult.GetError().c_str()));
        return false;
    }
    JPH::ShapeRefC meshShape = meshResult.Get();

    JPH::BodyCreationSettings worldSettings(meshShape, JPH::RVec3::sZero(), JPH::Quat::sIdentity(),
        JPH::EMotionType::Static, Layers::NON_MOVING);
    JPH::BodyInterface& bodyInterface = m_PhysicsSystem.GetBodyInterface();
    JPH::Body* worldBody = bodyInterface.CreateBody(worldSettings);
    if (worldBody == nullptr) {
        std::cerr << "[JoltWorld] Failed to create world body\n";
        g_Console.logError("Failed to create world body");
        return false;
    }
    m_WorldBody = worldBody->GetID();
    bodyInterface.AddBody(m_WorldBody, JPH::EActivation::DontActivate);

    // Capsule whose bottom sits at the character position (feet). Height 1.8m, radius 0.35m.
    JPH::Ref<JPH::CapsuleShape> capsule = new JPH::CapsuleShape(
        0.5f * (m_CharacterHeight - 2.0f * m_CharacterRadius), m_CharacterRadius);

    JPH::CharacterVirtualSettings charSettings;
    charSettings.mUp = JPH::Vec3::sAxisY();
    charSettings.mShape = capsule;
    charSettings.mShapeOffset = JPH::Vec3(0.0f, m_CharacterHeight * 0.5f, 0.0f);
    // Reduced from 45 to 35 degrees to prevent climbing vertical walls when pressing
    // forward + jumping. 35 deg is still walkable for stairs/ramps but walls (90 deg)
    // are now too steep for the character to climb.
    charSettings.mMaxSlopeAngle = JPH::DegreesToRadians(35.0f);
    charSettings.mCharacterPadding = 0.02f;
    charSettings.mPredictiveContactDistance = 0.1f;
    charSettings.mPenetrationRecoverySpeed = 1.0f;

    m_Character = new JPH::CharacterVirtual(&charSettings,
        JPH::RVec3(playerStart.x, playerStart.y, playerStart.z),
        JPH::Quat::sIdentity(), 0, &m_PhysicsSystem);

    m_Initialized = true;
    return true;
}

void JoltWorld::shutdown() {
    m_Initialized = false;
    m_CollisionMesh = nullptr;

    m_Character = nullptr; // released before the physics system below
    m_TempAllocator.reset();

    if (!m_WorldBody.IsInvalid()) {
        JPH::BodyInterface& bodyInterface = m_PhysicsSystem.GetBodyInterface();
        bodyInterface.RemoveBody(m_WorldBody);
        bodyInterface.DestroyBody(m_WorldBody);
        m_WorldBody = JPH::BodyID();
    }

    m_BpLayerInterface.reset();
    m_ObjVsBpFilter.reset();
    m_ObjPairFilter.reset();

    if (JPH::Factory::sInstance != nullptr) {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

void JoltWorld::step(float deltaTime, glm::vec3& position, glm::vec3& velocity, bool& grounded) {
    if (!m_Initialized || m_Character == nullptr)
        return;

    const glm::vec3 startPos = position;
    const glm::vec3 desiredVel = velocity;

    // Jolt owns all movement and collision: the GoldSrc controller only feeds the desired
    // velocity in, Jolt integrates gravity and resolves the capsule against the world.
    // Gravity integration is the caller's job.
    float velY = velocity.y - m_Gravity * deltaTime;
    if (velY < -m_MaxFallSpeed)
        velY = -m_MaxFallSpeed;

    m_Character->SetPosition(JPH::RVec3(position.x, position.y, position.z));
    m_Character->SetLinearVelocity(JPH::Vec3(velocity.x, velY, velocity.z));

    JPH::CharacterVirtual::ExtendedUpdateSettings settings;
    settings.mStickToFloorStepDown = JPH::Vec3(0.0f, -0.5f, 0.0f);
    // Stair climbing is handled deterministically by tryStepUp below; Jolt's own
    // WalkStairs is disabled so the two mechanisms can never fight each other.
    settings.mWalkStairsStepUp = JPH::Vec3::sZero();
    // Prevent the character from climbing vertical walls by ensuring the max slope
    // angle is respected during the extended update. The character's mMaxSlopeAngle
    // (set to 35 degrees in init) is used internally by Jolt to reject wall contacts.

    const JPH::Vec3 gravity(0.0f, -m_Gravity, 0.0f);
    m_Character->ExtendedUpdate(deltaTime, gravity, settings,
        m_PhysicsSystem.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
        m_PhysicsSystem.GetDefaultLayerFilter(Layers::MOVING),
        JPH::BodyFilter(), JPH::ShapeFilter(), *m_TempAllocator);

    const JPH::RVec3 pos = m_Character->GetPosition();
    position.x = (float)pos.GetX();
    position.y = (float)pos.GetY();
    position.z = (float)pos.GetZ();

    m_GroundNormal = m_Character->GetGroundNormal();
    // Only a floor within the walkable slope limit counts as grounded. Jolt's IsSupported()
    // also returns true while leaning on / sliding up a wall (OnSteepGround), which would
    // let the player chain-jump up walls with W + Space (the coyote timer never expires).
    grounded = m_Character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;

    // If we are grounded but got blocked mid-stride, the blocker is treated as a stair:
    // lift the feet, step forward and drop onto the tread. Fully deterministic and only
    // fires for a height we can actually clear.
    const glm::vec3 desiredHoriz(desiredVel.x * deltaTime, 0.0f, desiredVel.z * deltaTime);
    glm::vec3 achievedHoriz = position - startPos;
    achievedHoriz.y = 0.0f;
    const float desiredLen = glm::length(desiredHoriz);
    if (grounded && m_CollisionMesh != nullptr && desiredLen > 1e-4f &&
        glm::length(achievedHoriz) < 0.5f * desiredLen) {
        glm::vec3 stepped;
        if (tryStepUp(position, desiredHoriz, stepped)) {
            position = stepped;
            m_Character->SetPosition(JPH::RVec3(stepped.x, stepped.y, stepped.z));
            m_Character->RefreshContacts(
                m_PhysicsSystem.GetDefaultBroadPhaseLayerFilter(Layers::MOVING),
                m_PhysicsSystem.GetDefaultLayerFilter(Layers::MOVING),
                JPH::BodyFilter(), JPH::ShapeFilter(), *m_TempAllocator);
            m_GroundNormal = m_Character->GetGroundNormal();
            grounded = m_Character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        }
    }

    const JPH::Vec3 vel = m_Character->GetLinearVelocity();
    velocity.x = vel.GetX();
    velocity.y = vel.GetY();
    velocity.z = vel.GetZ();
}

bool JoltWorld::tryStepUp(const glm::vec3& start, const glm::vec3& horiz, glm::vec3& out) const {
    if (m_CollisionMesh == nullptr)
        return false;

    const CollisionMesh& mesh = *m_CollisionMesh;
    const float lift = m_StepHeight;
    const float radius = m_CharacterRadius;
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 down(0.0f, -1.0f, 0.0f);

    const float fwdLen = glm::length(horiz);
    if (fwdLen < 1e-4f)
        return false;
    const glm::vec3 dir = horiz / fwdLen;

    // 1) Headroom: after lifting by `lift`, the head must stay clear.
    if (mesh.raycast(start + up * lift, up, m_CharacterHeight - lift).hit)
        return false;

    // 2) Forward clearance at capsule height after the lift. A blocker that still reaches
    //    the head is a wall, not a stair.
    {
        const glm::vec3 checkOrigin = start + up * (lift + 0.5f * m_CharacterHeight);
        const float checkDist = fwdLen + radius;
        const RaycastHit wall = mesh.raycast(checkOrigin, dir, checkDist);
        if (wall.hit && wall.distance < checkDist - 0.05f)
            return false;
    }

    // 3) The blocker in front must be a steep riser, not a shallow ramp (a ramp is walked
    //    normally). Riser faces have a near-horizontal normal.
    {
        const RaycastHit riser = mesh.raycast(start + up * 0.15f, dir, fwdLen + radius);
        if (!riser.hit || glm::dot(riser.normal, up) >= 0.5f)
            return false;
    }

    // 4) Find the step tread under the front edge of the capsule (moved forward by one
    //    radius so the capsule rests on the tread) and make sure it really is a step:
    //    no higher than `lift`, but a real height gain, and the landing must be a
    //    surface you can actually stand on. Wall sides, wall bottoms and steep slopes
    //    have a near-horizontal normal, so they can never qualify as a tread.
    {
        const glm::vec3 feetTarget = start + dir * (radius + fwdLen);
        const RaycastHit ground = mesh.raycast(feetTarget + up * (lift + 0.05f), down, lift + 0.25f);
        if (!ground.hit)
            return false;
        const float newY = ground.point.y;
        if (newY < start.y + 0.02f || newY > start.y + lift + 0.02f)
            return false;
        if (glm::dot(ground.normal, up) < 0.9f)
            return false;
        out = glm::vec3(feetTarget.x, newY, feetTarget.z);
        return true;
    }
}

glm::vec3 JoltWorld::groundNormal() const {
    return glm::vec3(m_GroundNormal.GetX(), m_GroundNormal.GetY(), m_GroundNormal.GetZ());
}
