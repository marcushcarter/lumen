#pragma once
#include <core/assets/guid.h>
#include <core/animation/anim_library.h>
#include <core/animation/dynamics.h>
#include <core/scene/scene_settings.h>
#include <core/scene/relevance.h>
#include <filesystem>
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
    struct Body {
        const LLocomotion* names = nullptr;
        Guid asset;
        float model_yaw = 0.0f;
    };

    struct Proxy {
        RagdollBody body;
        uint64_t key = 0;
        uint32_t generation = 0;
        uint64_t frame = 0;
        vec3 a[RAGDOLL_PART_COUNT] = {};
        vec3 b[RAGDOLL_PART_COUNT] = {};
        float radius[RAGDOLL_PART_COUNT] = {};
    };

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
    AnimLibrary anims;
    AnimEvaluator evaluator;
    std::vector<JointTransform> _local;
    std::vector<mat4> _global;
    RelevanceView _view;
    DynamicsMode _mode = DynamicsMode::SIMULATE;
    DynamicsWorld _world;
    std::vector<Consumer> _consumers;
    std::unordered_map<uint32_t, Proxy> _proxies;
    uint64_t _frame = 0;

    void load(const std::filesystem::path& p_content_dir, const std::filesystem::path& p_assets_dir);
    void update(Scene& r_world, const Renderer& p_renderer, float p_dt, DynamicsMode p_mode);
    void clear();

    const Binding* _binding(const Renderer& p_renderer, uint32_t p_mesh_index);
    static float _mesh_radius(const Renderer& p_renderer, uint32_t p_mesh_index, const mat4& p_model);
    bool _active(const vec3& p_center, float p_radius) const;
    bool _touches(const vec3& p_center, float p_radius) const;
    void _gather_consumers(Scene& r_world, const Renderer& p_renderer);
    void _build_world(Scene& r_world, const Renderer& p_renderer);
    void _record_proxy(Entity p_entity, const LLocomotion& p_locomotion, const LSkeleton& p_skeleton, Guid p_asset, const mat4& p_model, float p_model_yaw);
    void _simulate(CharacterDynamics& r_dynamics, const LSkeleton& p_skeleton, const LMeshRig& p_rig, const std::vector<int32_t>& p_map, Guid p_mesh, const mat4& p_model, float p_radius, float p_dt, const PhysicsSettings& p_settings, const Body& p_body, uint32_t p_owner, PoseComponent& r_pose);
    Body _body_of(const Scene& p_world, Entity p_entity);
};

}
