#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

namespace lumen {

using namespace glm;

enum class CollisionType : uint8_t { MESH, HULL, BOX, SPHERE, CAPSULE, COUNT };

const char* collision_type_name(CollisionType p_type);
const char* collision_type_label(CollisionType p_type);
CollisionType collision_type_from_name(const std::string& p_name, CollisionType p_fallback);

struct LCollisionPayloadHeader
{
    uint32_t type;
    uint32_t vertex_count;
    uint32_t triangle_count;
    uint32_t source_triangle_count;
    vec4 bounds_min;
    vec4 bounds_max;
    vec4 center;
    vec4 rotation;
    vec4 half_extents;
};

static_assert(sizeof(LCollisionPayloadHeader) == 96, "LCollisionPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LCollisionPayloadHeader>, "must be blittable");

struct LCollision
{
    Guid guid;
    CollisionType type = CollisionType::MESH;
    std::vector<vec3> vertices;
    std::vector<uint32_t> indices;
    uint32_t source_triangles = 0;
    vec3 bounds_min = vec3(0.0f);
    vec3 bounds_max = vec3(0.0f);
    vec3 center = vec3(0.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    vec3 half_extents = vec3(0.0f);
    float radius = 0.0f;
    float half_height = 0.0f;

    uint32_t triangle_count() const { return (uint32_t)(indices.size() / 3); }
    bool primitive() const { return type == CollisionType::BOX || type == CollisionType::SPHERE || type == CollisionType::CAPSULE; }

    std::vector<uint8_t> to_payload() const;
    Error from_payload(const uint8_t* p_data, size_t p_size);
};

}
