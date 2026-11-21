#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "water/draw.glsl"

layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_world;
layout(location = 2) out vec2 v_flow;
layout(location = 3) out float v_foam;

const vec2 CORNERS[6] = vec2[](vec2(0.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));

vec4 sample_at(WaterItem p_it, uint p_index) {
    const uint o = p_index * 4u;
    return vec4(p_it.samples.v[o], p_it.samples.v[o + 1u], p_it.samples.v[o + 2u], p_it.samples.v[o + 3u]);
}

float height_at(WaterItem p_it, vec2 p_s) {
    const vec2 s = clamp(p_s, vec2(0.0), vec2(16.0));
    const ivec2 i0 = min(ivec2(s), ivec2(15));
    const vec2 t = s - vec2(i0);
    const uint b = uint(i0.y) * SURFACE_ROW + uint(i0.x);
    const float h00 = p_it.samples.v[b * 4u];
    const float h10 = p_it.samples.v[(b + 1u) * 4u];
    const float h01 = p_it.samples.v[(b + SURFACE_ROW) * 4u];
    const float h11 = p_it.samples.v[(b + SURFACE_ROW + 1u) * 4u];
    return mix(mix(h00, h10, t.x), mix(h01, h11, t.x), t.y);
}

vec2 flow_at(WaterItem p_it, vec2 p_s) {
    const vec2 s = clamp(p_s, vec2(0.0), vec2(16.0));
    const ivec2 i0 = min(ivec2(s), ivec2(15));
    const vec2 t = s - vec2(i0);
    const uint b = uint(i0.y) * SURFACE_ROW + uint(i0.x);
    const vec2 f00 = unpackHalf2x16(floatBitsToUint(sample_at(p_it, b).z));
    const vec2 f10 = unpackHalf2x16(floatBitsToUint(sample_at(p_it, b + 1u).z));
    const vec2 f01 = unpackHalf2x16(floatBitsToUint(sample_at(p_it, b + SURFACE_ROW).z));
    const vec2 f11 = unpackHalf2x16(floatBitsToUint(sample_at(p_it, b + SURFACE_ROW + 1u).z));
    return mix(mix(f00, f10, t.x), mix(f01, f11, t.x), t.y);
}

float foam_at(WaterItem p_it, vec2 p_s) {
    const vec2 s = clamp(p_s, vec2(0.0), vec2(16.0));
    const ivec2 i0 = min(ivec2(s), ivec2(15));
    const vec2 t = s - vec2(i0);
    const uint b = uint(i0.y) * SURFACE_ROW + uint(i0.x);
    const float a00 = p_it.samples.v[b * 4u + 3u];
    const float a10 = p_it.samples.v[(b + 1u) * 4u + 3u];
    const float a01 = p_it.samples.v[(b + SURFACE_ROW) * 4u + 3u];
    const float a11 = p_it.samples.v[(b + SURFACE_ROW + 1u) * 4u + 3u];
    return mix(mix(a00, a10, t.x), mix(a01, a11, t.x), t.y);
}

bool cell_row_visible(WaterItem p_it, uint p_row, uint p_c0, uint p_count) {
    const uint word = p_it.masks.v[p_row >> 1u];
    const uint bits = (word >> ((p_row & 1u) * 16u)) & 0xFFFFu;
    const uint range = p_count >= 16u ? 0xFFFFu : ((1u << p_count) - 1u);
    return ((bits >> p_c0) & range) != 0u;
}

void main() {
    const uint entry_index = uint(gl_InstanceIndex);
    const uint level = entry_index / pc.capacity;
    const uint entry = pc.list.v[entry_index];
    WaterItem it = pc.items.v[entry >> 4u];
    const uvec2 block = uvec2(entry & 3u, (entry >> 2u) & 3u);
    const uint qb = 1u << level;
    const uint vi = uint(gl_VertexIndex);
    const uint quad = vi / 6u;
    const uvec2 qc = uvec2(quad % qb, quad / qb);
    const float step = float(WATER_BLOCK_CELLS) / float(qb);
    const vec2 block_origin = vec2(block * WATER_BLOCK_CELLS);

    bool visible = false;
    if (qb >= WATER_BLOCK_CELLS) {
        const uvec2 cell = uvec2(block_origin + vec2(qc) * step);
        visible = cell_row_visible(it, cell.y, cell.x, 1u);
    } else {
        const uint span = uint(step);
        const uvec2 c0 = block * WATER_BLOCK_CELLS + qc * span;
        for (uint r = 0u; r < span && !visible; r++) visible = cell_row_visible(it, c0.y + r, c0.x, span);
    }
    if (!visible) {
        v_normal = vec3(0.0, 1.0, 0.0);
        v_world = vec3(0.0);
        v_flow = vec2(0.0);
        v_foam = 0.0;
        gl_Position = vec4(0.0);
        return;
    }

    const vec2 gv = vec2(block * qb + qc) + CORNERS[vi % 6u];
    vec2 s = gv * step;
    {
        const vec2 w0 = it.origin + s * it.spacing;
        const float y0 = height_at(it, s);
        const float d = distance(pc.camera.position.xyz, vec3(w0.x, y0, w0.y));
        const float quad_size = step * it.spacing;
        const float k = clamp((d * pc.lod_error / quad_size - WATER_MORPH_START) / (1.0 - WATER_MORPH_START), 0.0, 1.0);
        const vec2 coarse = floor(gv * 0.5) * 2.0 * step;
        s = mix(s, coarse, k);
    }
    const float y = height_at(it, s);
    const float e = 0.5;
    const float hx = height_at(it, s + vec2(e, 0.0)) - height_at(it, s - vec2(e, 0.0));
    const float hz = height_at(it, s + vec2(0.0, e)) - height_at(it, s - vec2(0.0, e));
    v_normal = normalize(vec3(-hx, 2.0 * e * it.spacing, -hz));

    const vec2 xz = it.origin + s * it.spacing;
    v_world = vec3(xz.x, y, xz.y);
    v_flow = flow_at(it, s);
    v_foam = foam_at(it, s);
    gl_Position = pc.camera.curr_view_proj * vec4(xz.x, y, xz.y, 1.0);
}
