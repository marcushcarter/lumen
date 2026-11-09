#include <core/animation/bone_debug.h>
#include <core/animation/skinning.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <core/world/world.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace lumen {

static constexpr float BONE_WAIST = 0.1f;

void debug_draw_bone(DebugDraw& r_draw, const vec3& p_head, const vec3& p_tail, uint32_t p_color)
{
    const DebugDraw::Style style{ .color = p_color, .category = DebugCategory::BONES, .xray = true };
    const vec3 axis = p_tail - p_head;
    const float len = length(axis);
    if (len < 1e-5f) return;

    const vec3 d = axis / len;
    const vec3 up = std::abs(d.y) < 0.99f ? vec3(0.0f, 1.0f, 0.0f) : vec3(1.0f, 0.0f, 0.0f);
    const vec3 u = normalize(cross(up, d));
    const vec3 v = cross(d, u);
    const vec3 waist = p_head + axis * BONE_WAIST;
    const float r = len * BONE_WAIST;
    const vec3 ring[4] = { waist + u * r, waist + v * r, waist - u * r, waist - v * r };

    for (uint32_t i = 0; i < 4; i++) {
        r_draw.line(p_head, ring[i], style);
        r_draw.line(ring[i], p_tail, style);
        r_draw.line(ring[i], ring[(i + 1) % 4], style);
    }
}

void debug_draw_bones(DebugDraw& r_draw, const World& p_world, const Renderer& p_renderer, uint32_t p_color)
{
    std::vector<int32_t> map;
    std::vector<vec3> joints;
    std::vector<uint8_t> valid;

    p_world.view<TransformComponent, ModelComponent, PoseComponent>(Exclude<StaticTag, EditorHiddenTag>{}, [&](Entity p_entity, const TransformComponent&, const ModelComponent& p_model, const PoseComponent& p_pose) {
        if (p_pose.skin.empty()) return;
        uint32_t mesh_index, slot_base;
        p_renderer.models.lookup(p_model.model, mesh_index, slot_base);
        const LMeshRig* rig = p_renderer.geometry.rig(mesh_index);
        if (!rig) return;
        const SkeletonCache::Entry* entry = p_renderer.skeletons.get(rig->skeleton);
        if (!entry) return;
        const LSkeleton& skeleton = entry->skeleton;

        rig_joint_map(skeleton, *rig, map);
        const uint32_t joint_count = skeleton.joint_count();
        joints.assign(joint_count, vec3(0.0f));
        valid.assign(joint_count, 0);

        const mat4 model = p_world.world_matrix(p_entity);
        const size_t count = std::min({ p_pose.skin.size(), rig->inverse_binds.size(), map.size() });
        for (size_t j = 0; j < count; j++) {
            const int32_t s = map[j];
            if (s == SKELETON_NO_JOINT || s >= (int32_t)joint_count) continue;
            const mat4 global = model * p_pose.skin[j] * inverse(rig->inverse_binds[j]);
            joints[s] = vec3(global[3]);
            valid[s] = 1;
        }

        for (uint32_t s = 0; s < joint_count; s++) {
            const int32_t parent = skeleton.parents[s];
            if (!valid[s] || parent == SKELETON_NO_JOINT || parent >= (int32_t)joint_count || !valid[parent]) continue;
            debug_draw_bone(r_draw, joints[parent], joints[s], p_color);
        }
    });
}

}
