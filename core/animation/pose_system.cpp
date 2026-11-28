#include <core/animation/pose_system.h>
#include <core/animation/skinning.h>
#include <core/base/hash.h>
#include <core/base/jobs.h>
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
    bool rebuilt = false;
    if (geometry_version != p_renderer.geometry.version || skeleton_version != p_renderer.skeletons.version) {
        bindings.clear();
        geometry_version = p_renderer.geometry.version;
        skeleton_version = p_renderer.skeletons.version;
        rebuilt = true;
    }

    std::vector<Entity> posed;
    r_world.view<MeshComponent>(Exclude<StaticTag, PoseComponent, DisabledTag>{}, [&](Entity p_entity, const MeshComponent& p_model) {
        if (p_renderer.geometry.rig(p_renderer.mesh_slots.mesh_index(p_model.mesh))) posed.push_back(p_entity);
    });
    for (Entity e : posed) r_world.add<PoseComponent>(e);

    _work.clear();
    r_world.view<MeshComponent, PoseComponent>(Exclude<StaticTag, DisabledTag>{}, [&](Entity, const MeshComponent& p_model, PoseComponent& r_pose) {
        const uint32_t mesh_index = p_renderer.mesh_slots.mesh_index(p_model.mesh);
        Work& w = _work.emplace_back();
        w.pose = &r_pose;
        w.binding = _binding(p_renderer, mesh_index);
        w.rig = p_renderer.geometry.rig(mesh_index);
        const SkeletonCache::Entry* entry = w.rig ? p_renderer.skeletons.get(w.rig->skeleton) : nullptr;
        w.skeleton = entry ? &entry->skeleton : nullptr;
        w.skeleton_guid = w.rig ? w.rig->skeleton : Guid{};
    });

    job_system().parallel_for((uint32_t)_work.size(), 4, [&](uint32_t p_index) { _skin(_work[p_index], rebuilt); }, JobPriority::HIGH, "Poses");
}

void PoseSystem::_skin(const Work& p_work, bool p_rebuilt)
{
    static thread_local std::vector<mat4> t_global;
    static thread_local std::vector<JointTransform> t_hidden;
    PoseComponent& r_pose = *p_work.pose;
    const Binding* binding = p_work.binding;
    const bool fresh = r_pose.joints_fresh;
    r_pose.joints_fresh = false;
    if (binding && r_pose.held && !p_rebuilt && r_pose.skin.size() == binding->rest_skin.size()) return;
    r_pose.skin_revision++;
    r_pose.view_skin.clear();
    if (!binding) {
        r_pose.skin.clear();
        return;
    }
    const LMeshRig* rig = p_work.rig;
    const LSkeleton* skeleton = p_work.skeleton;
    if (!skeleton || r_pose.skeleton != p_work.skeleton_guid || r_pose.local.size() != skeleton->joint_count()) {
        r_pose.skin = binding->rest_skin;
        return;
    }
    if (fresh && r_pose.joints.size() == r_pose.local.size()) {
        rig_skin(r_pose.joints, *rig, binding->map, r_pose.skin);
    } else {
        t_global.resize(r_pose.local.size());
        skeleton->globals(r_pose.local.data(), t_global.data());
        rig_skin(t_global, *rig, binding->map, r_pose.skin);
    }
    if (r_pose.hidden_joint < 0 || r_pose.hidden_joint >= (int32_t)r_pose.local.size()) return;
    t_hidden = r_pose.local;
    t_hidden[r_pose.hidden_joint].scale = vec3(0.001f);
    t_global.resize(t_hidden.size());
    skeleton->globals(t_hidden.data(), t_global.data());
    rig_skin(t_global, *rig, binding->map, r_pose.view_skin);
}

}
