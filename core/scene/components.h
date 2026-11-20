#pragma once
#include <core/assets/guid.h>
#include <core/physics/physics_types.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace lumen {

using namespace glm;

struct EntityIdComponent { Guid guid; };

struct NameComponent
{
    static constexpr uint32_t MAX_LENGTH = 64;
    char name[MAX_LENGTH] = {};
};

struct TransformComponent
{
    vec3 position = vec3(0.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    vec3 scale = vec3(1.0f);
};

struct ModelComponent
{
    Guid model;
    vec4 tint = vec4(1.0f);
};

struct PoseComponent
{
    std::vector<mat4> skin;
};

enum class CollisionShapeType : uint8_t { BOX, SPHERE, CAPSULE, CYLINDER, TAPERED_CYLINDER, TAPERED_CAPSULE, WEDGE, COUNT };
enum class CollisionSource : uint8_t { MESH, SHAPES, COUNT };

static constexpr uint32_t COLLISION_MAX_SHAPES = 8;

struct CollisionShapeEntry
{
    CollisionShapeType type = CollisionShapeType::BOX;
    vec3 position = vec3(0.0f);
    vec3 rotation = vec3(0.0f);
    vec3 half_extents = vec3(0.5f);
    float radius = 0.5f;
    float half_height = 0.5f;
    float top_radius = 0.25f;
};

struct CollisionComponent
{
    CollisionSource source = CollisionSource::MESH;
    PhysicsMotion motion = PhysicsMotion::STATIC;
    float mass = 10.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    bool trigger = false;
    uint32_t shape_count = 0;
    CollisionShapeEntry shapes[COLLISION_MAX_SHAPES];
};

enum class CharacterController : uint8_t { NONE, PLAYER, AI };

struct InitialVelocityComponent
{
    vec3 linear = vec3(0.0f);
    vec3 angular = vec3(0.0f);
};

struct CharacterComponent
{
    CharacterController controller = CharacterController::NONE;
};

struct OrbitCameraComponent
{
    float distance = 3.0f;
    float shoulder = 0.35f;
    float pivot_height = 1.6f;
    float radius = 0.2f;
    float pitch_min = -70.0f;
    float pitch_max = 60.0f;
};

enum class WaterSourceType : uint8_t { SPRING, LEVEL, COUNT };

struct WaterSourceComponent
{
    WaterSourceType type = WaterSourceType::SPRING;
    vec3 half_extents = vec3(1.0f);
    float flow = 1.0f;
};

struct ParentComponent
{
    Guid parent;
};

struct TemplateInstanceComponent
{
    Guid tmpl;
};

struct TemplatePartComponent
{
    Guid instance;
    Guid source;
};

struct StaticTag {};
struct DisabledTag {};
struct EditorExpandedTag {};

}
