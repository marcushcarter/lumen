#pragma once
#include <core/assets/lcollision.h>
#include <core/base/error.h>
#include <glm/glm.hpp>
#include <filesystem>

struct aiScene;

namespace lumen {

struct EditorContext;

struct CollisionSettings
{
    static constexpr float DEFAULT_ERROR = 0.02f;
    static constexpr float DEFAULT_MIN_FEATURE = 0.15f;

    CollisionType type = CollisionType::MESH;
    float error = DEFAULT_ERROR;
    float min_feature = DEFAULT_MIN_FEATURE;
    bool exclude_masked = true;

    bool operator==(const CollisionSettings&) const = default;
};

struct CollisionCooker
{
    static constexpr int MAX_HULL_VERTICES = 256;
    static constexpr float MIN_PRIMITIVE_SIZE = 0.001f;

    static bool build(const aiScene* p_scene, const CollisionSettings& p_settings, const glm::mat4& p_import, LCollision& r_collision);
    static bool has_collision(const std::filesystem::path& p_lmesh);
    static Error reimport(EditorContext& ctx, const std::filesystem::path& p_lmesh, const CollisionSettings& p_settings);
    static Error remove(EditorContext& ctx, const std::filesystem::path& p_lmesh);
};

}
