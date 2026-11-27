#include <core/animation/pose_system.h>
#include <core/animation/skinning.h>
#include <core/base/hash.h>
#include <core/rendering/renderer.h>
#include <core/scene/components.h>
#include <core/scene/scene.h>

namespace lumen {

void PoseSystem::clear()
{
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

void PoseSystem::update(Scene& r_world, const Renderer& p_renderer)
{
    if (geometry_version != p_renderer.geometry.version || skeleton_version != p_renderer.skeletons.version) {
        bindings.clear();
        geometry_version = p_renderer.geometry.version;
        skeleton_version = p_renderer.skeletons.version;
    }

    std::vector<Entity> posed;
    r_world.view<MeshComponent>(Exclude<StaticTag, PoseComponent, DisabledTag>{}, [&](Entity p_entity, const MeshComponent& p_model) {
        if (p_renderer.geometry.rig(p_renderer.mesh_slots.mesh_index(p_model.mesh))) posed.push_back(p_entity);
    });
    for (Entity e : posed) r_world.add<PoseComponent>(e);

    r_world.view<MeshComponent, PoseComponent>(Exclude<StaticTag, DisabledTag>{}, [&](Entity, const MeshComponent& p_model, PoseComponent& r_pose) {
        const Binding* binding = _binding(p_renderer, p_renderer.mesh_slots.mesh_index(p_model.mesh));
        r_pose.view_skin.clear();
        if (!binding) {
            r_pose.skin.clear();
            return;
        }
        const LMeshRig* rig = p_renderer.geometry.rig(p_renderer.mesh_slots.mesh_index(p_model.mesh));
        const SkeletonCache::Entry* skeleton = rig ? p_renderer.skeletons.get(rig->skeleton) : nullptr;
        if (!skeleton || r_pose.skeleton != rig->skeleton || r_pose.local.size() != skeleton->skeleton.joint_count()) {
            r_pose.skin = binding->rest_skin;
            return;
        }
        _global.resize(r_pose.local.size());
        skeleton->skeleton.globals(r_pose.local.data(), _global.data());
        rig_skin(_global, *rig, binding->map, r_pose.skin);
        if (r_pose.hidden_joint < 0 || r_pose.hidden_joint >= (int32_t)r_pose.local.size()) return;
        _hidden = r_pose.local;
        _hidden[r_pose.hidden_joint].scale = vec3(0.001f);
        skeleton->skeleton.globals(_hidden.data(), _global.data());
        rig_skin(_global, *rig, binding->map, r_pose.view_skin);
    });
}

}
