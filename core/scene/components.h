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

struct LocomotionComponent
{
    Guid locomotion;
    float radius = 0.3f;
    float height = 1.8f;
    float crouch_height = 1.2f;
    float model_yaw = 0.0f;
    float max_step_height = 0.45f;
    float max_slope = 45.0f;
};

struct PlayerTag {};

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
