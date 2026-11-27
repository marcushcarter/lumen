#pragma once
#include <core/assets/lskeleton.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lumen {

using namespace glm;

struct Scene;
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
    std::vector<mat4> _global;
    std::vector<JointTransform> _hidden;

    void update(Scene& r_world, const Renderer& p_renderer);
    void clear();

    const Binding* _binding(const Renderer& p_renderer, uint32_t p_mesh_index);
};

}
