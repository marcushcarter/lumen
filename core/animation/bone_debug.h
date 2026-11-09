#pragma once
#include <core/rendering/debug_draw.h>

namespace lumen {

struct World;
struct Renderer;

void debug_draw_bone(DebugDraw& r_draw, const vec3& p_head, const vec3& p_tail, uint32_t p_color);
void debug_draw_bones(DebugDraw& r_draw, const World& p_world, const Renderer& p_renderer, uint32_t p_color = DebugDraw::CYAN);

}
