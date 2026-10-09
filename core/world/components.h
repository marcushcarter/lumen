#pragma once
#include <core/assets/guid.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace lumen {

using namespace glm;

struct EntityIdComponent { Guid guid; };

struct NameComponent {
    static constexpr uint32_t MAX_LENGTH = 64;
    char name[MAX_LENGTH] = {};
};

struct TransformComponent {
    vec3 position = vec3(0.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    vec3 scale = vec3(1.0f);
};

struct MeshComponent {
    Guid mesh;
};

struct MeshGridComponent {
    Guid mesh;
    uvec3 count = uvec3(10, 1, 10);
    float spacing = 0.0f;
};

struct EditorFolderComponent {
    static constexpr uint32_t ROOT = 0xFFFFFFFF;
    uint32_t folder = ROOT;
};

struct StaticTag {};
struct EditorHiddenTag {};

}
