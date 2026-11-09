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

static constexpr float INSTANCE_SATURATION = 0.6f;
static constexpr float INSTANCE_VALUE = 1.0f;
static constexpr float GOLDEN_RATIO_CONJUGATE = 0.618033988749895f;

void debug_draw_bone(DebugDraw& r_draw, const vec3& p_head, const vec3& p_tail, uint32_t p_color)
{
    r_draw.line(p_head, p_tail, { .color = p_color, .category = DebugCategory::BONES, .xray = true });
}

void debug_draw_joint(DebugDraw& r_draw, const vec3& p_position, uint32_t p_color)
{
    r_draw.point(p_position, BONE_JOINT_RADIUS_PX, { .color = p_color, .category = DebugCategory::BONES, .xray = true });
}

uint32_t debug_instance_color(uint64_t p_seed)
{
    uint64_t z = p_seed + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    const float hue = std::fmod((float)(z & 0xFFFFu) / 65536.0f + GOLDEN_RATIO_CONJUGATE, 1.0f) * 6.0f;
    const float c = INSTANCE_VALUE * INSTANCE_SATURATION;
    const float x = c * (1.0f - std::abs(std::fmod(hue, 2.0f) - 1.0f));
    const float m = INSTANCE_VALUE - c;
    vec3 rgb;
    if (hue < 1.0f) rgb = vec3(c, x, 0.0f);
    else if (hue < 2.0f) rgb = vec3(x, c, 0.0f);
    else if (hue < 3.0f) rgb = vec3(0.0f, c, x);
    else if (hue < 4.0f) rgb = vec3(0.0f, x, c);
    else if (hue < 5.0f) rgb = vec3(x, 0.0f, c);
    else rgb = vec3(c, 0.0f, x);
    rgb += vec3(m);
    const uint32_t r = (uint32_t)(std::clamp(rgb.r, 0.0f, 1.0f) * 255.0f + 0.5f);
    const uint32_t g = (uint32_t)(std::clamp(rgb.g, 0.0f, 1.0f) * 255.0f + 0.5f);
    const uint32_t b = (uint32_t)(std::clamp(rgb.b, 0.0f, 1.0f) * 255.0f + 0.5f);
    return debug_rgb((r << 16) | (g << 8) | b);
}

void debug_draw_bones(DebugDraw& r_draw, const World& p_world, const Renderer& p_renderer)
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

        const EntityIdComponent* id = p_world.try_get<EntityIdComponent>(p_entity);
        const uint32_t color = debug_instance_color(id ? id->guid.value : (uint64_t)p_entity.index);
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
            if (!valid[s]) continue;
            debug_draw_joint(r_draw, joints[s], color);
            const int32_t parent = skeleton.parents[s];
            if (parent == SKELETON_NO_JOINT || parent >= (int32_t)joint_count || !valid[parent]) continue;
            debug_draw_bone(r_draw, joints[parent], joints[s], color);
        }
    });
}

}
