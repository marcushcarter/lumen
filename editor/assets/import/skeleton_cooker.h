#pragma once
#include <editor/assets/import/mesh_cooker.h>
#include <core/assets/lskeleton.h>

struct aiScene;

namespace lumen {

struct SkeletonCooker
{
    static bool build(const aiScene* p_scene, const MeshCooker::CookSettings& p_settings, LSkeleton& r_skeleton);
};

}
