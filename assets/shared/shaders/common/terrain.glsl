#ifndef TERRAIN_GLSL
#define TERRAIN_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint TERRAIN_TILE_QUADS = 256u;
const uint TERRAIN_APRON = 1u;
const uint TERRAIN_COARSE_LEVEL = 4u;
const uint TERRAIN_COARSE_STEP = 16u;
const uint TERRAIN_LEVELS = 5u;
const uint TERRAIN_LOD_NONE = 0xFFFFFFFFu;
const uint TERRAIN_QUARTER_QUADS = 8u;

struct TerrainPatch {
    uint origin_x;
    uint origin_z;
    uint tile;
    uint packed;
};

struct TerrainTile {
    uint word_base;
    uint lod;
};

layout(buffer_reference, scalar) buffer TerrainHeightBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer TerrainTileBuffer { TerrainTile data[]; };
layout(buffer_reference, scalar) buffer TerrainPatchBuffer { TerrainPatch data[]; };

layout(buffer_reference, scalar) buffer TerrainBuffer {
    vec4 origin_spacing;
    vec4 morph_camera;
    vec4 morph[TERRAIN_LEVELS];
    float height_min;
    float height_scale;
    uint tiles_x;
    uint tiles_y;
    uint samples_x;
    uint samples_y;
    uint coarse_w;
    uint coarse_h;
    TerrainHeightBuffer heights;
    TerrainTileBuffer tile_table;
    TerrainPatchBuffer patches;
    uint _pad0;
    uint _pad1;
};

float terrain_u16(TerrainHeightBuffer p_heights, uint p_word_base, uint p_index) {
    const uint w = p_heights.data[p_word_base + (p_index >> 1u)];
    return float((w >> ((p_index & 1u) * 16u)) & 0xFFFFu);
}

uvec2 terrain_tile_origin(TerrainBuffer p_t, uint p_tile) {
    return uvec2(p_tile % p_t.tiles_x, p_tile / p_t.tiles_x) * TERRAIN_TILE_QUADS;
}

float terrain_fetch(TerrainBuffer p_t, uint p_tile, uvec2 p_tile_origin, uint p_level, uvec2 p_i) {
    if (p_level >= TERRAIN_COARSE_LEVEL) {
        const uvec2 c = min(p_tile_origin / TERRAIN_COARSE_STEP + p_i * (1u << (p_level - TERRAIN_COARSE_LEVEL)), uvec2(p_t.coarse_w, p_t.coarse_h) - 1u);
        return terrain_u16(p_t.heights, 0u, c.y * p_t.coarse_w + c.x);
    }
    const TerrainTile e = p_t.tile_table.data[p_tile];
    const uint r = e.lod;
    const uvec2 s = p_i * (1u << (p_level - r)) + TERRAIN_APRON;
    const uint row = (TERRAIN_TILE_QUADS >> r) + 1u + 2u * TERRAIN_APRON;
    return terrain_u16(p_t.heights, e.word_base, s.y * row + s.x);
}

float terrain_level_height(TerrainBuffer p_t, uint p_tile, uint p_level, vec2 p_sample) {
    const uvec2 origin = terrain_tile_origin(p_t, p_tile);
    const float step = float(1u << p_level);
    const float n = float(TERRAIN_TILE_QUADS >> p_level);
    const vec2 local = clamp((p_sample - vec2(origin)) / step, vec2(0.0), vec2(n));
    const vec2 c0 = min(floor(local), vec2(n - 1.0));
    const vec2 f = local - c0;
    const uvec2 i = uvec2(c0);
    float h = terrain_fetch(p_t, p_tile, origin, p_level, i);
    if (f.x > 0.0 || f.y > 0.0) {
        const float h10 = terrain_fetch(p_t, p_tile, origin, p_level, i + uvec2(1u, 0u));
        const float h01 = terrain_fetch(p_t, p_tile, origin, p_level, i + uvec2(0u, 1u));
        const float h11 = terrain_fetch(p_t, p_tile, origin, p_level, i + uvec2(1u, 1u));
        h = mix(mix(h, h10, f.x), mix(h01, h11, f.x), f.y);
    }
    return p_t.height_min + h * p_t.height_scale;
}

uint terrain_tile_at(TerrainBuffer p_t, vec2 p_sample) {
    const uvec2 t = uvec2(clamp(floor(p_sample / float(TERRAIN_TILE_QUADS)), vec2(0.0), vec2(p_t.tiles_x - 1u, p_t.tiles_y - 1u)));
    return t.y * p_t.tiles_x + t.x;
}

float terrain_height(TerrainBuffer p_t, vec2 p_sample, out float r_step) {
    const uint tile = terrain_tile_at(p_t, p_sample);
    const uint lod = p_t.tile_table.data[tile].lod;
    const uint level = lod == TERRAIN_LOD_NONE ? TERRAIN_COARSE_LEVEL : lod;
    r_step = float(1u << level);
    return terrain_level_height(p_t, tile, level, p_sample);
}

uint terrain_sample_index(TerrainBuffer p_t, TerrainTile p_entry, uvec2 p_tile_origin, uint p_level, uvec2 p_i, out uint r_word_base) {
    if (p_level >= TERRAIN_COARSE_LEVEL) {
        const uvec2 c = p_tile_origin / TERRAIN_COARSE_STEP + p_i;
        r_word_base = 0u;
        return c.y * p_t.coarse_w + c.x;
    }
    const uvec2 s = p_i + TERRAIN_APRON;
    const uint row = (TERRAIN_TILE_QUADS >> p_level) + 1u + 2u * TERRAIN_APRON;
    r_word_base = p_entry.word_base;
    return s.y * row + s.x;
}

vec4 terrain_run4(TerrainBuffer p_t, TerrainTile p_entry, uvec2 p_tile_origin, uint p_level, uvec2 p_i) {
    uint base;
    const uint index = terrain_sample_index(p_t, p_entry, p_tile_origin, p_level, p_i, base);
    const uint w = base + (index >> 1u);
    const uint w0 = p_t.heights.data[w];
    const uint w1 = p_t.heights.data[w + 1u];
    if ((index & 1u) == 0u) return vec4(float(w0 & 0xFFFFu), float(w0 >> 16u), float(w1 & 0xFFFFu), float(w1 >> 16u));
    const uint w2 = p_t.heights.data[w + 2u];
    return vec4(float(w0 >> 16u), float(w1 & 0xFFFFu), float(w1 >> 16u), float(w2 & 0xFFFFu));
}

vec2 terrain_run2(TerrainBuffer p_t, TerrainTile p_entry, uvec2 p_tile_origin, uint p_level, uvec2 p_i) {
    uint base;
    const uint index = terrain_sample_index(p_t, p_entry, p_tile_origin, p_level, p_i, base);
    const uint w = base + (index >> 1u);
    const uint w0 = p_t.heights.data[w];
    if ((index & 1u) == 0u) return vec2(float(w0 & 0xFFFFu), float(w0 >> 16u));
    return vec2(float(w0 >> 16u), float(p_t.heights.data[w + 1u] & 0xFFFFu));
}

vec3 terrain_normal(TerrainBuffer p_t, vec3 p_world) {
    const float spacing = p_t.origin_spacing.w;
    const vec2 p = (p_world.xz - p_t.origin_spacing.xz) / spacing;
    const uint tile = terrain_tile_at(p_t, p);
    const TerrainTile entry = p_t.tile_table.data[tile];
    const uint lod = entry.lod;
    const uint level = lod == TERRAIN_LOD_NONE ? TERRAIN_COARSE_LEVEL : lod;
    const float step = float(1u << level);

    const uvec2 origin = terrain_tile_origin(p_t, tile);
    const float n = float(TERRAIN_TILE_QUADS >> level);
    const vec2 local = (p - vec2(origin)) / step;
    const vec2 c0 = floor(local);
    if (all(greaterThanEqual(c0, vec2(1.0))) && all(lessThanEqual(c0, vec2(n - 2.0)))) {
        const vec2 f = local - c0;
        const uvec2 c = uvec2(c0);
        const vec4 a = terrain_run4(p_t, entry, origin, level, uvec2(c.x - 1u, c.y));
        const vec4 b = terrain_run4(p_t, entry, origin, level, uvec2(c.x - 1u, c.y + 1u));
        const vec2 d = terrain_run2(p_t, entry, origin, level, uvec2(c.x, c.y - 1u));
        const vec2 u = terrain_run2(p_t, entry, origin, level, uvec2(c.x, c.y + 2u));
        const float hl = mix(mix(a.x, a.y, f.x), mix(b.x, b.y, f.x), f.y);
        const float hr = mix(mix(a.z, a.w, f.x), mix(b.z, b.w, f.x), f.y);
        const float hd = mix(mix(d.x, d.y, f.x), mix(a.y, a.z, f.x), f.y);
        const float hu = mix(mix(b.y, b.z, f.x), mix(u.x, u.y, f.x), f.y);
        return normalize(vec3((hl - hr) * p_t.height_scale, 2.0 * step * spacing, (hd - hu) * p_t.height_scale));
    }

    float unused;
    const float hl = terrain_height(p_t, p - vec2(step, 0.0), unused);
    const float hr = terrain_height(p_t, p + vec2(step, 0.0), unused);
    const float hd = terrain_height(p_t, p - vec2(0.0, step), unused);
    const float hu = terrain_height(p_t, p + vec2(0.0, step), unused);
    return normalize(vec3(hl - hr, 2.0 * step * spacing, hd - hu));
}

#endif
