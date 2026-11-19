#include <core/animation/pose_system.h>
#include <core/animation/skinning.h>
#include <core/base/hash.h>
#include <core/physics/collision_preview.h>
#include <core/rendering/renderer.h>
#include <core/scene/components.h>
#include <core/scene/scene.h>
#include <algorithm>

namespace lumen {

void PoseSystem::clear()
{
    bindings.clear();
    _world.clear();
    _consumers.clear();
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
    _mode = p_mode;
    _view = relevance_view(p_renderer.main_view.camera);
    _view.shadow = normalize(EnvironmentSettings::SUN_DIRECTION);
    r_world.view<TransformComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const TransformComponent&) {
        if (_view.count < RelevanceView::MAX_POINTS && character_is_player(r_world, p_entity)) _view.add(vec3(r_world.world_matrix(p_entity)[3]));
    });
    if (geometry_version != p_renderer.geometry.version || skeleton_version != p_renderer.skeletons.version) {
        bindings.clear();
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

    _gather_consumers(r_world, p_renderer);
    _build_world(r_world, p_renderer);

    r_world.view<ModelComponent, PoseComponent, DynamicsComponent>(Exclude<StaticTag, DisabledTag>{}, [&](Entity p_entity, const ModelComponent& p_model, PoseComponent& r_pose, DynamicsComponent& r_dynamics) {
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
        _simulate(r_dynamics.dynamics, skeleton->skeleton, *rig, binding->map, p_renderer.geometry.mesh_guids[mesh_index], model, _mesh_radius(p_renderer, mesh_index, model), p_dt, r_world.physics, p_entity.index, r_pose);
    });
}

static float _max_scale(const mat4& p_m)
{
    return std::max(length(vec3(p_m[0])), std::max(length(vec3(p_m[1])), length(vec3(p_m[2]))));
}

static DynamicsBox _box(const mat4& p_m, const vec3& p_center, const vec3& p_half)
{
    DynamicsBox box;
    box.center = vec3(p_m * vec4(p_center, 1.0f));
    for (int k = 0; k < 3; k++) {
        const float len = length(vec3(p_m[k]));
        if (len > 1e-6f) box.axis[k] = vec3(p_m[k]) / len;
        box.half[k] = p_half[k] * len;
    }
    return box;
}

bool PoseSystem::_active(const vec3& p_center, float p_radius) const
{
    return relevance_visible(_view, p_center, p_radius) && relevance_interval(relevance_band(relevance_distance(_view, p_center, p_radius), p_radius)) != 0;
}

bool PoseSystem::_touches(const vec3& p_center, float p_radius) const
{
    for (const Consumer& c : _consumers) {
        const vec3 d = c.center - p_center;
        const float reach = c.radius + p_radius;
        if (dot(d, d) <= reach * reach) return true;
    }
    return false;
}

void PoseSystem::_gather_consumers(Scene& r_world, const Renderer& p_renderer)
{
    _consumers.clear();
    if (_mode != DynamicsMode::SIMULATE || !(r_world.physics.chains || r_world.physics.cloth)) return;
    r_world.view<ModelComponent, DynamicsComponent>(Exclude<StaticTag, DisabledTag>{}, [&](Entity p_entity, const ModelComponent& p_model, const DynamicsComponent&) {
        const uint32_t mesh_index = p_renderer.models.mesh_index(p_model.model);
        const LMesh* mesh = p_renderer.geometry.get(mesh_index);
        const LMeshRig* rig = p_renderer.geometry.rig(mesh_index);
        const SkeletonCache::Entry* skeleton = rig ? p_renderer.skeletons.get(rig->skeleton) : nullptr;
        if (!mesh || !skeleton) return;
        const mat4 model = r_world.world_matrix(p_entity);
        const float radius = mesh->bounds_sphere.w * _max_scale(model);
        if (!_active(vec3(model[3]), radius)) return;
        _consumers.push_back(Consumer{ vec3(model * vec4(vec3(mesh->bounds_sphere), 1.0f)), radius + dynamics_reach(skeleton->skeleton) });
    });
}

void PoseSystem::_build_world(Scene& r_world, const Renderer& p_renderer)
{
    _world.clear();
    if (_consumers.empty()) return;

    r_world.view<CollisionComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const CollisionComponent& p_collision) {
        if (p_collision.motion == PhysicsMotion::STATIC || p_collision.trigger) return;
        const mat4 m = r_world.world_matrix(p_entity);
        const float scale = _max_scale(m);
        if (p_collision.source == CollisionSource::SHAPES) {
            const uint32_t count = std::min(p_collision.shape_count, COLLISION_MAX_SHAPES);
            float reach = 0.0f;
            for (uint32_t i = 0; i < count; i++) {
                const CollisionShapeEntry& e = p_collision.shapes[i];
                reach = std::max(reach, length(e.position) + std::max({ length(e.half_extents), e.radius + e.half_height, e.top_radius + e.half_height }));
            }
            if (!_touches(vec3(m[3]), reach * scale)) return;
            _world.begin(p_entity.index);
            for (uint32_t i = 0; i < count; i++) {
                const CollisionShapeEntry& e = p_collision.shapes[i];
                const mat4 w = m * collision_shape_matrix(e);
                const vec3 center = vec3(w[3]);
                const vec3 up = vec3(w[1]);
                const bool tapered = e.type == CollisionShapeType::TAPERED_CAPSULE || e.type == CollisionShapeType::TAPERED_CYLINDER;
                const float r = tapered ? std::max(e.radius, e.top_radius) : e.radius;
                switch (e.type) {
                    case CollisionShapeType::SPHERE:
                        _world.capsule(center, center, e.radius * scale);
                        break;
                    case CollisionShapeType::CAPSULE:
                    case CollisionShapeType::TAPERED_CAPSULE:
                        _world.capsule(center - up * e.half_height, center + up * e.half_height, r * scale);
                        break;
                    case CollisionShapeType::CYLINDER:
                    case CollisionShapeType::TAPERED_CYLINDER: {
                        const float half = std::max(e.half_height - r, 0.0f);
                        _world.capsule(center - up * half, center + up * half, r * scale);
                        break;
                    }
                    default:
                        _world.box(_box(w, vec3(0.0f), e.half_extents));
                        break;
                }
            }
            _world.end();
            return;
        }
        const ModelComponent* model = r_world.try_get<ModelComponent>(p_entity);
        const LMesh* mesh = model ? p_renderer.geometry.get(p_renderer.models.mesh_index(model->model)) : nullptr;
        if (!mesh || !_touches(vec3(m * vec4(vec3(mesh->bounds_sphere), 1.0f)), mesh->bounds_sphere.w * scale)) return;
        _world.begin(p_entity.index);
        _world.box(_box(m, mesh->pos_min + mesh->pos_extent * 0.5f, mesh->pos_extent * 0.5f));
        _world.end();
    });
}

float PoseSystem::_mesh_radius(const Renderer& p_renderer, uint32_t p_mesh_index, const mat4& p_model)
{
    const LMesh* mesh = p_renderer.geometry.get(p_mesh_index);
    if (!mesh) return 1.0f;
    const float scale = std::max(length(vec3(p_model[0])), std::max(length(vec3(p_model[1])), length(vec3(p_model[2]))));
    return mesh->bounds_sphere.w * scale;
}

void PoseSystem::_simulate(CharacterDynamics& r_dynamics, const LSkeleton& p_skeleton, const LMeshRig& p_rig, const std::vector<int32_t>& p_map, Guid p_mesh, const mat4& p_model, float p_radius, float p_dt, const PhysicsSettings& p_settings, uint32_t p_owner, PoseComponent& r_pose)
{
    const uint64_t key = hash_combine(hash_combine(p_mesh.value, p_rig.skeleton.value), skeleton_version ^ (geometry_version << 1));
    r_dynamics.bind(key, p_skeleton, p_rig);

    const bool enabled_chains = p_settings.chains && _mode != DynamicsMode::REST;
    const bool enabled_cloth = p_settings.cloth && _mode != DynamicsMode::REST;
    bool active = _mode == DynamicsMode::SIMULATE;
    float dt = 0.0f;
    if (active) {
        const vec3 center = vec3(p_model[3]);
        const uint32_t interval = relevance_visible(_view, center, p_radius) ? relevance_interval(relevance_band(relevance_distance(_view, center, p_radius), p_radius)) : 0;
        active = interval != 0;
        r_dynamics.step_limit = std::min(2u * interval, 4u);
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

    if (active && (enabled_chains || enabled_cloth)) {
        r_dynamics.update_colliders(p_skeleton, _global, p_model);
        if (!_world.sources.empty()) {
            const float reach = dynamics_reach(p_skeleton);
            r_dynamics.gather_world(_world, p_owner, vec3(p_model[3]), 2.0f * p_radius + reach, reach);
        }
    }
    if (enabled_chains) r_dynamics.simulate_chains(p_skeleton, p_model, dt, p_settings.gravity, _local.data(), _global);
    else r_dynamics.follow_chains(p_model, _global);
    rig_skin(_global, p_rig, p_map, r_pose.skin);
    r_dynamics.simulate_cloth(p_skeleton, p_rig, p_model, dt, p_settings.gravity, enabled_cloth, r_pose.skin);
    r_dynamics.finish(p_model);
}

}
