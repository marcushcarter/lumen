#pragma once
#include <core/assets/guid.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace lumen {

using namespace glm;

struct EntityIdComponent
{
    Guid guid;
};

struct TransformComponent
{
    vec3 position = vec3(0.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    vec3 scale = vec3(1.0f);
};

struct MeshComponent
{
    static constexpr uint32_t INVALID_INDEX = 0xFFFFFFFF;

    Guid mesh;
    uint32_t mesh_index = INVALID_INDEX;
};

struct StaticTag
{
};

// struct TransformComponent
// {
//     Guid guid;
//     vec3 position;
//     quat rotation;
//     vec3 scale;
//     mat4 previous_transform;
// };

// struct StaticMeshComponent
// {
//     Guid guid;
// };

// struct DynamicMeshComponent
// {
//     Guid guid;

// };

}