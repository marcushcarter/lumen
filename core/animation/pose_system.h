#pragma once
#include <core/assets/guid.h>
#include <core/assets/lskeleton.h>
#include <core/animation/dynamics.h>
#include <core/scene/scene_settings.h>
#include <core/scene/relevance.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lumen {

using namespace glm;

struct Scene;
struct Entity;
struct Renderer;
struct PoseComponent;

enum class DynamicsMode : uint8_t { REST, HOLD, SIMULATE };

struct PoseSystem
{
    struct Consumer {
        vec3 center = vec3(0.0f);
        float radius = 0.0f;
    };

    struct Binding {
        std::vector<int32_t> map;
        std::vector<mat4> rest_skin;
    };

    std::unordered_map<uint64_t, Binding> bindings;
    uint64_t geometry_version = UINT64_MAX;
    uint64_t skeleton_version = UINT64_MAX;
    std::vector<JointTransform> _local;
    std::vector<mat4> _global;
    RelevanceView _view;
    DynamicsMode _mode = DynamicsMode::SIMULATE;
    DynamicsWorld _world;
    std::vector<Consumer> _consumers;

    void update(Scene& r_world, const Renderer& p_renderer, float p_dt, DynamicsMode p_mode);
    void clear();

    const Binding* _binding(const Renderer& p_renderer, uint32_t p_mesh_index);
    static float _mesh_radius(const Renderer& p_renderer, uint32_t p_mesh_index, const mat4& p_model);
    bool _active(const vec3& p_center, float p_radius) const;
    bool _touches(const vec3& p_center, float p_radius) const;
    void _gather_consumers(Scene& r_world, const Renderer& p_renderer);
    void _build_world(Scene& r_world, const Renderer& p_renderer);
    void _simulate(CharacterDynamics& r_dynamics, const LSkeleton& p_skeleton, const LMeshRig& p_rig, const std::vector<int32_t>& p_map, Guid p_mesh, const mat4& p_model, float p_radius, float p_dt, const PhysicsSettings& p_settings, uint32_t p_owner, PoseComponent& r_pose);
};

}
