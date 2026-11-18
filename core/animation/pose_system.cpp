#include <core/animation/pose_system.h>
#include <core/game/character.h>
#include <core/animation/skinning.h>
#include <core/base/hash.h>
#include <core/rendering/renderer.h>
#include <core/scene/components.h>
#include <core/scene/scene.h>
#include <algorithm>

namespace lumen {

void PoseSystem::load(const std::filesystem::path& p_content_dir, const std::filesystem::path& p_assets_dir)
{
    clear();
    anims.load(p_content_dir, p_assets_dir);
}

void PoseSystem::clear()
{
    anims.clear();
    bindings.clear();
    geometry_version = UINT64_MAX;
    skeleton_version = UINT64_MAX;
}

const PoseSystem::Binding* PoseSystem::_binding(const Renderer& p_renderer, uint32_t p_mesh_index)
{
    const LMeshRig* rig = p_renderer.geometry.rig(p_mesh_index);
    if (!rig) return nullptr;
    const SkeletonCache::Entry* skeleton = p_renderer.skeletons.get(rig->skeleton);
    if (!skeleton) return nullptr;

    const uint64_t key = hash_combine(p_renderer.geometry.mesh_guids[p_mesh_index].value, rig->skeleton.value);
    auto [it, added] = bindings.try_emplace(key);
    if (added) {
        rig_joint_map(skeleton->skeleton, *rig, it->second.map);
        rig_skin(skeleton->rest_global, *rig, it->second.map, it->second.rest_skin);
    }
    return &it->second;
}

void PoseSystem::update(Scene& r_world, const Renderer& p_renderer, float p_dt, DynamicsMode p_mode)
{
    anims.tick(p_dt);
    _mode = p_mode;
    _view = relevance_view(p_renderer.main_view.camera);
    _view.shadow = normalize(EnvironmentSettings::SUN_DIRECTION);
    r_world.view<TransformComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const TransformComponent&) {
        if (_view.count < RelevanceView::MAX_POINTS && r_world.has<PlayerTag>(p_entity)) _view.add(vec3(r_world.world_matrix(p_entity)[3]));
    });
    if (geometry_version != p_renderer.geometry.version || skeleton_version != p_renderer.skeletons.version) {
        bindings.clear();
        anims.unbind();
        geometry_version = p_renderer.geometry.version;
        skeleton_version = p_renderer.skeletons.version;
    }

    std::vector<Entity> posed;
    r_world.view<ModelComponent>(Exclude<StaticTag, PoseComponent, DisabledTag>{}, [&](Entity p_entity, const ModelComponent& p_model) {
        uint32_t mesh_index, slot_base;
        p_renderer.models.lookup(p_model.model, mesh_index, slot_base);
        if (p_renderer.geometry.rig(mesh_index)) posed.push_back(p_entity);
    });
    for (Entity e : posed) r_world.add<PoseComponent>(e);

    r_world.view<ModelComponent, PoseComponent>(Exclude<StaticTag, DisabledTag>{}, [&](Entity, const ModelComponent& p_model, PoseComponent& r_pose) {
        uint32_t mesh_index, slot_base;
        p_renderer.models.lookup(p_model.model, mesh_index, slot_base);
        const Binding* binding = _binding(p_renderer, mesh_index);
        if (!binding) {
            r_pose.skin.clear();
            return;
        }
        r_pose.skin = binding->rest_skin;
    });

    std::vector<Entity> dynamic;
    r_world.view<ModelComponent, PoseComponent>(Exclude<StaticTag, DisabledTag, DynamicsComponent>{}, [&](Entity p_entity, const ModelComponent& p_model, PoseComponent&) {
        const LMeshRig* rig = p_renderer.geometry.rig(p_renderer.models.mesh_index(p_model.model));
        const SkeletonCache::Entry* skeleton = rig ? p_renderer.skeletons.get(rig->skeleton) : nullptr;
        if (skeleton && (!skeleton->skeleton.chains.empty() || !skeleton->skeleton.cloths.empty())) dynamic.push_back(p_entity);
    });
    for (Entity e : dynamic) r_world.add<DynamicsComponent>(e);

    r_world.view<ModelComponent, PoseComponent, DynamicsComponent>(Exclude<StaticTag, DisabledTag, CharacterStateComponent>{}, [&](Entity p_entity, const ModelComponent& p_model, PoseComponent& r_pose, DynamicsComponent& r_dynamics) {
        if (r_pose.skin.empty()) return;
        uint32_t mesh_index, slot_base;
        p_renderer.models.lookup(p_model.model, mesh_index, slot_base);
        const LMeshRig* rig = p_renderer.geometry.rig(mesh_index);
        const Binding* binding = _binding(p_renderer, mesh_index);
        const SkeletonCache::Entry* skeleton = rig ? p_renderer.skeletons.get(rig->skeleton) : nullptr;
        if (!binding || !skeleton) return;
        _local = skeleton->skeleton.rest;
        _global = skeleton->rest_global;
        const mat4 model = r_world.world_matrix(p_entity);
        _simulate(r_dynamics.dynamics, skeleton->skeleton, *rig, binding->map, p_renderer.geometry.mesh_guids[mesh_index], model, _mesh_radius(p_renderer, mesh_index, model), p_dt, r_world.physics, _body_of(r_world, p_entity), r_pose);
    });

    r_world.view<ModelComponent, PoseComponent, CharacterStateComponent>(Exclude<StaticTag, DisabledTag>{}, [&](Entity p_entity, const ModelComponent& p_model, PoseComponent& r_pose, CharacterStateComponent& r_state) {
        if (r_pose.skin.empty()) return;
        const LLocomotion* locomotion = anims.locomotion(r_state.character.asset);
        if (!locomotion) return;
        uint32_t mesh_index, slot_base;
        p_renderer.models.lookup(p_model.model, mesh_index, slot_base);
        const LMeshRig* rig = p_renderer.geometry.rig(mesh_index);
        const Binding* binding = _binding(p_renderer, mesh_index);
        const SkeletonCache::Entry* skeleton = rig ? p_renderer.skeletons.get(rig->skeleton) : nullptr;
        if (!binding || !skeleton) return;
        const uint32_t joints = skeleton->skeleton.joint_count();
        CharacterRagdoll& ragdoll = r_state.character.ragdoll;
        ragdoll.bind(*locomotion, skeleton->skeleton);
        if (ragdoll.state == RagdollState::ACTIVE && ragdoll.snapshot.size() == joints) {
            _local = ragdoll.snapshot;
            ragdoll.apply_pose(skeleton->skeleton, _local.data(), _global);
            ragdoll.record(_local.data(), _global, p_dt);
        } else {
            anims.skeleton = &skeleton->skeleton;
            r_state.character.action.bind(anims, skeleton->skeleton);
            _local = skeleton->skeleton.rest;
            const bool posed = character_pose(*locomotion, r_state.character, anims, evaluator, skeleton->skeleton, p_dt, _local.data());
            if (posed && ragdoll.state == RagdollState::GETTING_UP) {
                ragdoll.apply_get_up(anims, joints, p_dt, _local.data());
                r_state.character.ik.started = false;
            }
            anims.skeleton = nullptr;
            if (!posed) return;
            if (ragdoll.state != RagdollState::GETTING_UP) character_foot_ik(*locomotion, r_state.character, skeleton->skeleton, _local.data(), _global);
            _global.resize(joints);
            skeleton->skeleton.globals(_local.data(), _global.data());
            ragdoll.record(_local.data(), _global, p_dt);
        }

        if (DynamicsComponent* dynamics = r_world.try_get<DynamicsComponent>(p_entity)) {
            const mat4 model = r_world.world_matrix(p_entity);
            _simulate(dynamics->dynamics, skeleton->skeleton, *rig, binding->map, p_renderer.geometry.mesh_guids[mesh_index], model, _mesh_radius(p_renderer, mesh_index, model), p_dt, r_world.physics, Body{ locomotion, r_state.character.asset, r_state.character.model_yaw }, r_pose);
        }
        else rig_skin(_global, *rig, binding->map, r_pose.skin);
    });
}

float PoseSystem::_mesh_radius(const Renderer& p_renderer, uint32_t p_mesh_index, const mat4& p_model)
{
    const LMesh* mesh = p_renderer.geometry.get(p_mesh_index);
    if (!mesh) return 1.0f;
    const float scale = std::max(length(vec3(p_model[0])), std::max(length(vec3(p_model[1])), length(vec3(p_model[2]))));
    return mesh->bounds_sphere.w * scale;
}

void PoseSystem::_simulate(CharacterDynamics& r_dynamics, const LSkeleton& p_skeleton, const LMeshRig& p_rig, const std::vector<int32_t>& p_map, Guid p_mesh, const mat4& p_model, float p_radius, float p_dt, const PhysicsSettings& p_settings, const Body& p_body, PoseComponent& r_pose)
{
    static const LLocomotion DEFAULT_BODY;
    const uint64_t key = hash_combine(hash_combine(hash_combine(p_mesh.value, p_rig.skeleton.value), skeleton_version ^ (geometry_version << 1)), p_body.asset.value);
    r_dynamics.bind(key, p_skeleton, p_rig, p_body.names ? *p_body.names : DEFAULT_BODY);

    const bool enabled_chains = p_settings.chains && _mode != DynamicsMode::REST;
    const bool enabled_cloth = p_settings.cloth && _mode != DynamicsMode::REST;
    bool active = _mode == DynamicsMode::SIMULATE;
    float dt = 0.0f;
    if (active) {
        const vec3 center = vec3(p_model[3]);
        const uint32_t interval = relevance_visible(_view, center, p_radius) ? relevance_interval(relevance_band(relevance_distance(_view, center, p_radius), p_radius)) : 0;
        active = interval != 0;
        if (active) {
            r_dynamics.lod_time += p_dt;
            if (++r_dynamics.lod_frame >= interval) {
                dt = r_dynamics.lod_time;
                r_dynamics.lod_time = 0.0f;
                r_dynamics.lod_frame = 0;
            }
        }
    }
    if (!active) {
        r_dynamics.lod_time = 0.0f;
        r_dynamics.lod_frame = 0;
        if (enabled_chains || enabled_cloth) r_dynamics.hold(p_model);
    }

    if (active && (enabled_chains || enabled_cloth)) r_dynamics.update_colliders(p_skeleton, _global, p_model, p_body.model_yaw);
    if (enabled_chains) r_dynamics.simulate_chains(p_skeleton, p_model, dt, p_settings.gravity, _local.data(), _global);
    else r_dynamics.follow_chains(p_model, _global);
    rig_skin(_global, p_rig, p_map, r_pose.skin);
    r_dynamics.simulate_cloth(p_skeleton, p_rig, p_model, dt, p_settings.gravity, enabled_cloth, r_pose.skin);
    r_dynamics.finish(p_model);
}

PoseSystem::Body PoseSystem::_body_of(const Scene& p_world, Entity p_entity)
{
    const LocomotionComponent* body = p_world.try_get<LocomotionComponent>(p_entity);
    if (!body) return {};
    return Body{ anims.locomotion(body->locomotion), body->locomotion, body->model_yaw };
}

}
