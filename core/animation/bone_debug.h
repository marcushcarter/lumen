#pragma once
#include <core/rendering/debug_draw.h>

namespace lumen {

struct World;
struct Renderer;

static constexpr float BONE_JOINT_RADIUS_PX = 2.5f;

void debug_draw_bone(DebugDraw& r_draw, const vec3& p_head, const vec3& p_tail, uint32_t p_color);
void debug_draw_joint(DebugDraw& r_draw, const vec3& p_position, uint32_t p_color);
uint32_t debug_instance_color(uint64_t p_seed);
void debug_draw_bones(DebugDraw& r_draw, const World& p_world, const Renderer& p_renderer);

}
