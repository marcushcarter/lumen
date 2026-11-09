#pragma once
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/assets/import/mesh_cooker.h>
#include <core/assets/lcollision.h>
#include <filesystem>

struct aiScene;

namespace lumen {

struct EditorContext;

struct CollisionCooker
{
    static constexpr int MAX_HULL_VERTICES = 256;
    static constexpr float MIN_PRIMITIVE_SIZE = 0.001f;

    static bool build(const aiScene* p_scene, const MeshCooker::CookSettings& p_mesh, LCollision& r_collision);
    static Error write(const std::filesystem::path& p_file, Guid p_mesh, LCollision& r_collision);
    static bool has_collision(const std::filesystem::path& p_lmesh);
    static Error reimport(EditorContext& ctx, const std::filesystem::path& p_lmesh, const CollisionSettings& p_settings);
};

}
