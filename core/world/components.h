#pragma once
#include <core/assets/guid.h>
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

struct StaticTag {};
struct EditorHiddenTag {};

struct EditorFolderComponent
{
    static constexpr uint32_t ROOT = 0xFFFFFFFF;
    uint32_t folder = ROOT;
};

}
