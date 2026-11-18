#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/camera.glsl"
#include "common/terrain.glsl"

layout(push_constant, scalar) uniform Push {
    CameraBuffer camera;
    TerrainBuffer terrain;
} pc;

layout(location = 0) flat out uint v_patch;

const uint QUARTER_ROW = TERRAIN_QUARTER_QUADS + 1u;

void main() {
    const TerrainPatch tp = pc.terrain.patches.data[gl_InstanceIndex];
    TerrainBuffer t = terrain_at(pc.terrain, terrain_patch_terrain(tp));
    const uint level = tp.packed & 15u;
    const uint quarter = (tp.packed >> 4u) & 3u;
    const uint vi = uint(gl_VertexIndex);
    const vec2 g = vec2(uvec2(quarter & 1u, quarter >> 1u) * TERRAIN_QUARTER_QUADS + uvec2(vi % QUARTER_ROW, vi / QUARTER_ROW));
    const float step = float(1u << level);
    const vec2 base = vec2(tp.origin_x, tp.origin_z);
    const vec2 limit = vec2(t.samples_x - 1u, t.samples_y - 1u);
    const float spacing = t.origin_spacing.w;
    const vec3 origin = t.origin_spacing.xyz;

    vec2 p = min(base + g * step, limit);
    float h = terrain_level_height(t, tp.tile, level, p);
    const vec2 odd = fract(g * 0.5);
    if (odd.x > 0.0 || odd.y > 0.0) {
        const vec3 w0 = origin + vec3(p.x * spacing, h, p.y * spacing);
        const vec4 m = t.morph[level];
        const float k = clamp((distance(w0, t.morph_camera.xyz) - m.x) * m.y, 0.0, 1.0);
        if (k > 0.0) {
            p = min(base + (g - odd * 2.0 * k) * step, limit);
            h = terrain_level_height(t, tp.tile, level, p);
        }
    }

    v_patch = uint(gl_InstanceIndex);
    gl_Position = pc.camera.curr_view_proj * vec4(origin + vec3(p.x * spacing, h, p.y * spacing), 1.0);
}
