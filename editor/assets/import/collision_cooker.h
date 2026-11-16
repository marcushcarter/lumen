#pragma once
#include <core/assets/lcollision.h>
#include <core/base/error.h>
#include <glm/glm.hpp>
#include <toml++/toml.hpp>
#include <filesystem>
#include <string>
#include <vector>

struct aiScene;

namespace lumen {

struct EditorContext;

struct CollisionShapeSettings
{
    std::string name;
    CollisionType type = CollisionType::BOX;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 half_extents = glm::vec3(0.5f);
    float radius = 0.5f;
    float half_height = 0.5f;
    float top_radius = 0.25f;

    bool operator==(const CollisionShapeSettings&) const = default;
};

struct CollisionSettings
{
    static constexpr float DEFAULT_ERROR = 0.02f;
    static constexpr float DEFAULT_MIN_FEATURE = 0.15f;

    CollisionType type = CollisionType::MESH;
    float error = DEFAULT_ERROR;
    float min_feature = DEFAULT_MIN_FEATURE;
    bool exclude_masked = true;
    std::vector<CollisionShapeSettings> shapes;

    bool operator==(const CollisionSettings&) const = default;
};

void collision_settings_read(const toml::table& p_import, CollisionSettings& r_settings);
void collision_settings_write(toml::table& r_import, const CollisionSettings& p_settings);
LCollisionShape collision_shape_cook(const CollisionShapeSettings& p_shape);
CollisionShapeSettings collision_shape_from(const LCollision& p_collision);

struct CollisionCooker
{
    static constexpr int MAX_HULL_VERTICES = 256;
    static constexpr float MIN_PRIMITIVE_SIZE = 0.001f;

    static bool build(const aiScene* p_scene, const CollisionSettings& p_settings, const glm::mat4& p_import, LCollision& r_collision);
    static bool has_collision(const std::filesystem::path& p_lmesh);
    static Error reimport(EditorContext& ctx, const std::filesystem::path& p_lmesh, const CollisionSettings& p_settings);
    static Error remove(EditorContext& ctx, const std::filesystem::path& p_lmesh);
    static Error write_shapes(EditorContext& ctx, const std::filesystem::path& p_lmesh, const CollisionSettings& p_settings);
};

}
