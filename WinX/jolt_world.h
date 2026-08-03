#pragma once
#include <glm/glm.hpp>
#include <memory>

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include "collision.h"

// Thin wrapper that makes Jolt Physics own the player's vertical movement: gravity,
// falling, landing, ground detection and floor sticking are all solved by Jolt while
// the horizontal GoldSrc-style controller keeps its hand-tuned feel.
class JoltWorld {
public:
    JoltWorld();
    ~JoltWorld();
    JoltWorld(const JoltWorld&) = delete;
    JoltWorld& operator=(const JoltWorld&) = delete;

    // Builds a static Jolt mesh from the level triangles and spawns the virtual character
    // at playerStart (feet position).
    bool init(const CollisionMesh& collisionMesh, const glm::vec3& playerStart);
    void shutdown();

    // Applies gravity to velocity.y, lets Jolt move and resolve the character against the
    // world, then writes back the resolved position / velocity / grounded state.
    void step(float deltaTime, glm::vec3& position, glm::vec3& velocity, bool& grounded);

    float gravity() const { return m_Gravity; }
    float playerHeight() const { return m_CharacterHeight; }
    float playerRadius() const { return m_CharacterRadius; }
    glm::vec3 groundNormal() const;

    bool initialized() const { return m_Initialized; }

private:
    // Detects that the grounded character got blocked mid-stride (a stair riser) and
    // teleports it onto the step: lift, move forward, drop onto the tread.
    bool tryStepUp(const glm::vec3& start, const glm::vec3& horiz, glm::vec3& out) const;

    struct BPLayerInterface;
    struct ObjVsBpFilter;
    struct ObjPairFilter;

    float m_Gravity = 21.5f;
    float m_MaxFallSpeed = 16.0f;
    float m_CharacterHeight = 1.8f;
    float m_CharacterRadius = 0.35f;
    float m_StepHeight = 0.5f;

    // Declared before the physics system so they are destroyed after it.
    std::unique_ptr<BPLayerInterface> m_BpLayerInterface;
    std::unique_ptr<ObjVsBpFilter> m_ObjVsBpFilter;
    std::unique_ptr<ObjPairFilter> m_ObjPairFilter;

    JPH::PhysicsSystem m_PhysicsSystem;
    std::unique_ptr<JPH::TempAllocatorImpl> m_TempAllocator;
    JPH::Ref<JPH::CharacterVirtual> m_Character;

    // Level mesh owned by main.cpp; used for the deterministic step-up queries.
    const CollisionMesh* m_CollisionMesh = nullptr;

    JPH::Vec3 m_GroundNormal{ 0.0f, 1.0f, 0.0f };
    JPH::BodyID m_WorldBody;
    bool m_Initialized = false;
};
