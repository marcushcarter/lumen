#include <core/animation/pose_system.h>
#include <core/animation/skinning.h>
#include <core/base/hash.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <core/world/world.h>

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

void PoseSystem::update(World& r_world, const Renderer& p_renderer, float p_dt)
{
    anims.tick(p_dt);
    if (geometry_version != p_renderer.geometry.version || skeleton_version != p_renderer.skeletons.version) {
        bindings.clear();
        for (auto& [guid, clip] : anims.clips) clip.maps.clear();
        geometry_version = p_renderer.geometry.version;
        skeleton_version = p_renderer.skeletons.version;
    }

    std::vector<Entity> posed;
    r_world.view<ModelComponent>(Exclude<StaticTag, PoseComponent>{}, [&](Entity p_entity, const ModelComponent& p_model) {
        uint32_t mesh_index, slot_base;
        p_renderer.models.lookup(p_model.model, mesh_index, slot_base);
        if (p_renderer.geometry.rig(mesh_index)) posed.push_back(p_entity);
    });
    for (Entity e : posed) r_world.add<PoseComponent>(e);

    r_world.view<ModelComponent, PoseComponent>(Exclude<StaticTag>{}, [&](Entity, const ModelComponent& p_model, PoseComponent& r_pose) {
        uint32_t mesh_index, slot_base;
        p_renderer.models.lookup(p_model.model, mesh_index, slot_base);
        const Binding* binding = _binding(p_renderer, mesh_index);
        if (!binding) {
            r_pose.skin.clear();
            return;
        }
        r_pose.skin = binding->rest_skin;
    });

    r_world.view<ModelComponent, PoseComponent, AnimationComponent>(Exclude<StaticTag>{}, [&](Entity, const ModelComponent& p_model, PoseComponent& r_pose, AnimationComponent& r_anim) {
        const LBlendSpace* space = anims.blend_space(r_anim.blend_space);
        if (!space || r_pose.skin.empty()) return;
        uint32_t mesh_index, slot_base;
        p_renderer.models.lookup(p_model.model, mesh_index, slot_base);
        const LMeshRig* rig = p_renderer.geometry.rig(mesh_index);
        const Binding* binding = _binding(p_renderer, mesh_index);
        const SkeletonCache::Entry* skeleton = rig ? p_renderer.skeletons.get(rig->skeleton) : nullptr;
        if (!binding || !skeleton) return;
        anims.skeleton = &skeleton->skeleton;
        _local = skeleton->skeleton.rest;
        const bool posed = evaluator.blend_pose(anims, skeleton->skeleton, *space, r_anim.speed, r_anim.direction, p_dt, r_anim.phase, _local.data());
        anims.skeleton = nullptr;
        if (!posed) return;
        _global.resize(skeleton->skeleton.joint_count());
        skeleton->skeleton.globals(_local.data(), _global.data());
        rig_skin(_global, *rig, binding->map, r_pose.skin);
    });
}

}
