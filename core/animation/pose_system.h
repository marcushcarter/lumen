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
struct LMeshRig;
struct PoseComponent;

struct PoseSystem
{
    struct Binding {
        std::vector<int32_t> map;
        std::vector<mat4> rest_skin;
    };

    struct Work {
        PoseComponent* pose = nullptr;
        const Binding* binding = nullptr;
        const LMeshRig* rig = nullptr;
        const LSkeleton* skeleton = nullptr;
        Guid skeleton_guid;
    };

    std::unordered_map<uint64_t, Binding> bindings;
    uint64_t geometry_version = UINT64_MAX;
    uint64_t skeleton_version = UINT64_MAX;
    std::vector<Work> _work;

    void update(Scene& r_world, const Renderer& p_renderer);
    void clear();

    const Binding* _binding(const Renderer& p_renderer, uint32_t p_mesh_index);
    static void _skin(const Work& p_work, bool p_rebuilt);
};

}
