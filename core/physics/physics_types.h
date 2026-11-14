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
    COUNT
};

constexpr uint32_t physics_layer_bit(PhysicsLayer p_layer) { return 1u << (uint32_t)p_layer; }

static constexpr uint32_t PHYSICS_QUERY_WORLD = physics_layer_bit(PhysicsLayer::STATIC) | physics_layer_bit(PhysicsLayer::DYNAMIC) | physics_layer_bit(PhysicsLayer::KINEMATIC);
static constexpr uint32_t PHYSICS_QUERY_ALL_SOLID = PHYSICS_QUERY_WORLD | physics_layer_bit(PhysicsLayer::CHARACTER);

enum class PhysicsMotion : uint8_t { STATIC, KINEMATIC, DYNAMIC };

enum class PhysicsShapeType : uint8_t { BOX, SPHERE, CAPSULE, MESH_COLLISION };

struct PhysicsShape
{
    PhysicsShapeType type = PhysicsShapeType::BOX;
    vec3 half_extents = vec3(0.5f);
    float radius = 0.5f;
    float half_height = 0.5f;
    Guid mesh;

    static PhysicsShape box(const vec3& p_half_extents) { return { PhysicsShapeType::BOX, p_half_extents, 0.0f, 0.0f }; }
    static PhysicsShape sphere(float p_radius) { return { PhysicsShapeType::SPHERE, vec3(0.0f), p_radius, 0.0f }; }
    static PhysicsShape capsule(float p_half_height, float p_radius) { return { PhysicsShapeType::CAPSULE, vec3(0.0f), p_radius, p_half_height }; }
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
