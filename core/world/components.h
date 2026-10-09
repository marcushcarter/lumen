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

enum class LightType : uint8_t { DIRECTIONAL, POINT, SPOT };

struct LightComponent {
    static constexpr uint8_t FLAG_CAST_SHADOWS = 1 << 0;
    static constexpr uint8_t FLAG_AFFECTS_GI = 1 << 1;

    LightType type = LightType::DIRECTIONAL;
    uint8_t flags = FLAG_CAST_SHADOWS | FLAG_AFFECTS_GI;
    vec3 color = vec3(1.0f);
    float intensity = 1.0f;
    float range = 10.0f;
    float source_radius = 0.0f;
    float angular_diameter = 0.53f;
    float inner_cone_angle = 20.0f;
    float outer_cone_angle = 30.0f;
    float fade_start = 0.0f;
    float fade_end = 0.0f;

    float shadow_bias = 1.0f;
    float shadow_normal_bias = 1.0f;
};

struct LightProfileComponent { Guid ies; };

struct StaticTag {};
struct EditorHiddenTag {};

struct EditorFolderComponent {
    static constexpr uint32_t ROOT = 0xFFFFFFFF;
    uint32_t folder = ROOT;
};

}
