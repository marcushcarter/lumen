#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <core/assets/guid.h>
#include <cstdint>
#include <vector>

namespace lumen {

using namespace glm;

enum class PhysicsLayer : uint8_t
{
    STATIC,
    DYNAMIC,
    KINEMATIC,
    CHARACTER,
    TRIGGER,
    RAGDOLL,
    COUNT
};

constexpr uint32_t physics_layer_bit(PhysicsLayer p_layer) { return 1u << (uint32_t)p_layer; }

static constexpr uint32_t PHYSICS_QUERY_WORLD = physics_layer_bit(PhysicsLayer::STATIC) | physics_layer_bit(PhysicsLayer::DYNAMIC) | physics_layer_bit(PhysicsLayer::KINEMATIC);

enum class PhysicsMotion : uint8_t { STATIC, KINEMATIC, DYNAMIC };

enum class PhysicsShapeType : uint8_t { BOX, SPHERE, CAPSULE, MESH_COLLISION, CYLINDER, TAPERED_CYLINDER, TAPERED_CAPSULE, WEDGE };

struct PhysicsShape
{
    PhysicsShapeType type = PhysicsShapeType::BOX;
    vec3 half_extents = vec3(0.5f);
    float radius = 0.5f;
    float half_height = 0.5f;
    float top_radius = 0.5f;
    Guid mesh;

    static PhysicsShape box(const vec3& p_half_extents) { return { PhysicsShapeType::BOX, p_half_extents, 0.0f, 0.0f }; }
    static PhysicsShape sphere(float p_radius) { return { PhysicsShapeType::SPHERE, vec3(0.0f), p_radius, 0.0f }; }
    static PhysicsShape capsule(float p_half_height, float p_radius) { return { PhysicsShapeType::CAPSULE, vec3(0.0f), p_radius, p_half_height }; }
    static PhysicsShape cylinder(float p_half_height, float p_radius) { return { PhysicsShapeType::CYLINDER, vec3(0.0f), p_radius, p_half_height, p_radius }; }
    static PhysicsShape tapered_cylinder(float p_half_height, float p_top, float p_bottom) { return { PhysicsShapeType::TAPERED_CYLINDER, vec3(0.0f), p_bottom, p_half_height, p_top }; }
    static PhysicsShape tapered_capsule(float p_half_height, float p_top, float p_bottom) { return { PhysicsShapeType::TAPERED_CAPSULE, vec3(0.0f), p_bottom, p_half_height, p_top }; }
    static PhysicsShape wedge(const vec3& p_half_extents) { return { PhysicsShapeType::WEDGE, p_half_extents, 0.0f, 0.0f, 0.0f }; }
    static PhysicsShape mesh_collision(Guid p_mesh) { PhysicsShape s; s.type = PhysicsShapeType::MESH_COLLISION; s.mesh = p_mesh; return s; }
};

struct PhysicsShapePart
{
    PhysicsShape shape;
    vec3 position = vec3(0.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
};

struct PhysicsBodyId
{
    static constexpr uint32_t INVALID = 0xFFFFFFFFu;
    uint32_t value = INVALID;

    bool valid() const { return value != INVALID; }
    bool operator==(const PhysicsBodyId&) const = default;
};

struct PhysicsBodyDesc
{
    PhysicsShape shape;
    std::vector<PhysicsShapePart> parts;
    vec3 position = vec3(0.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    vec3 scale = vec3(1.0f);
    PhysicsMotion motion = PhysicsMotion::STATIC;
    PhysicsLayer layer = PhysicsLayer::STATIC;
    float mass = 0.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    bool sensor = false;
    uint64_t user_data = 0;
};

struct PhysicsCharacterId
{
    static constexpr uint32_t INVALID = 0xFFFFFFFFu;
    uint32_t value = INVALID;

    bool valid() const { return value != INVALID; }
    bool operator==(const PhysicsCharacterId&) const = default;
};

struct PhysicsRagdollId
{
    static constexpr uint32_t INVALID = 0xFFFFFFFFu;
    uint32_t value = INVALID;

    bool valid() const { return value != INVALID; }
    bool operator==(const PhysicsRagdollId&) const = default;
};

enum class PhysicsJoint : uint8_t { SWING_TWIST, HINGE };

struct PhysicsRagdollPart
{
    int32_t parent = -1;
    vec3 position = vec3(0.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    PhysicsShapePart shape;
    float mass = 1.0f;
    vec3 linear_velocity = vec3(0.0f);
    vec3 angular_velocity = vec3(0.0f);
    PhysicsJoint joint = PhysicsJoint::SWING_TWIST;
    vec3 joint_position = vec3(0.0f);
    vec3 parent_axis = vec3(1.0f, 0.0f, 0.0f);
    vec3 parent_normal = vec3(0.0f, 1.0f, 0.0f);
    vec3 child_axis = vec3(1.0f, 0.0f, 0.0f);
    vec3 child_normal = vec3(0.0f, 1.0f, 0.0f);
    float swing_normal = 0.0f;
    float swing_plane = 0.0f;
    float twist_min = 0.0f;
    float twist_max = 0.0f;
    float joint_friction = 0.0f;
};

struct PhysicsRagdollDesc
{
    std::vector<PhysicsRagdollPart> parts;
    float friction = 0.6f;
    float linear_damping = 0.05f;
    float angular_damping = 0.3f;
    float self_collision_gap = 0.05f;
    uint64_t user_data = 0;
};

struct PhysicsCharacterMove
{
    vec3 velocity = vec3(0.0f);
    float step_up = 0.45f;
    float stick_down = 0.5f;
};

struct PhysicsCharacterResult
{
    vec3 position = vec3(0.0f);
    vec3 velocity = vec3(0.0f);
    vec3 ground_normal = vec3(0.0f, 1.0f, 0.0f);
    bool on_ground = false;
    bool on_steep_ground = false;
};

struct PhysicsQuery
{
    uint32_t layers = PHYSICS_QUERY_WORLD;
    PhysicsBodyId ignore;
};

struct PhysicsHit
{
    vec3 position = vec3(0.0f);
    vec3 normal = vec3(0.0f, 1.0f, 0.0f);
    float distance = 0.0f;
    float fraction = 0.0f;
    PhysicsBodyId body;
    uint64_t user_data = 0;
};

}
