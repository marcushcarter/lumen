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

struct ModelGridComponent
{
    Guid model;
    uvec3 count = uvec3(10, 1, 10);
    float spacing = 0.0f;
};

// One skinning matrix per skeleton joint, rest-pose mesh space to posed mesh space.
// Only read for dynamic entities whose mesh is skinned.
struct PoseComponent
{
    std::vector<mat4> skin;
};

struct AnimationComponent
{
    Guid blend_space;
    float phase = 0.0f;
    float speed = 0.0f;
    float direction = 0.0f;
};

enum class CollisionShapeType : uint8_t { BOX, SPHERE, CAPSULE, MODEL, COUNT };

static constexpr uint32_t COLLISION_MAX_SHAPES = 8;

struct CollisionShapeEntry
{
    CollisionShapeType type = CollisionShapeType::BOX;
    vec3 position = vec3(0.0f);
    vec3 rotation = vec3(0.0f);
    vec3 half_extents = vec3(0.5f);
    float radius = 0.5f;
    float half_height = 0.5f;
};

struct CollisionComponent
{
    PhysicsMotion motion = PhysicsMotion::STATIC;
    float mass = 10.0f;
    float friction = 0.5f;
    float restitution = 0.0f;
    bool trigger = false;
    uint32_t shape_count = 1;
    CollisionShapeEntry shapes[COLLISION_MAX_SHAPES];
};

struct ParentComponent
{
    Guid parent;
};

struct PrefabInstanceComponent
{
    Guid prefab;
};

struct PrefabPartComponent
{
    Guid instance;
    Guid source;
};

struct StaticTag {};
struct EditorHiddenTag {};
struct EditorExpandedTag {};

}
