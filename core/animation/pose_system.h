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

    void load(const std::filesystem::path& p_content_dir, const std::filesystem::path& p_assets_dir);
    void update(Scene& r_world, const Renderer& p_renderer, float p_dt, DynamicsMode p_mode);
    void clear();

    const Binding* _binding(const Renderer& p_renderer, uint32_t p_mesh_index);
    static float _mesh_radius(const Renderer& p_renderer, uint32_t p_mesh_index, const mat4& p_model);
    void _simulate(CharacterDynamics& r_dynamics, const LSkeleton& p_skeleton, const LMeshRig& p_rig, const std::vector<int32_t>& p_map, Guid p_mesh, const mat4& p_model, float p_radius, float p_dt, const PhysicsSettings& p_settings, const Body& p_body, PoseComponent& r_pose);
    Body _body_of(const Scene& p_world, Entity p_entity);
};

}
