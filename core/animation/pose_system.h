#pragma once
#include <core/assets/guid.h>
#include <core/animation/anim_library.h>
#include <filesystem>
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lumen {

using namespace glm;

struct World;
struct Renderer;

struct PoseSystem
{
    struct Binding {
        std::vector<int32_t> map;
        std::vector<mat4> rest_skin;
    };

    std::unordered_map<uint64_t, Binding> bindings;
    uint64_t geometry_version = UINT64_MAX;
    uint64_t skeleton_version = UINT64_MAX;
    AnimLibrary anims;
    AnimEvaluator evaluator;
    std::vector<JointTransform> _local;
    std::vector<mat4> _global;

    void load(const std::filesystem::path& p_content_dir, const std::filesystem::path& p_assets_dir);
    void update(World& r_world, const Renderer& p_renderer, float p_dt);
    void clear();

    const Binding* _binding(const Renderer& p_renderer, uint32_t p_mesh_index);
};

}
