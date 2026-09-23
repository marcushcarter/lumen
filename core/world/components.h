#pragma once
#include <core/assets/guid.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace lumen {

using namespace glm;

struct TransformComponent
{
    Guid guid;
    vec3 position;
    quat rotation;
    vec3 scale;
    mat4 previous_transform;
};

struct StaticMeshComponent
{
    Guid guid;
};

struct DynamicMeshComponent
{
    Guid guid;

};

}