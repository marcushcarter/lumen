#ifndef VISBUFFER_GLSL
#define VISBUFFER_GLSL

const uint VIS_TERRAIN_BIT = 1u << 31;
const uint VIS_PHASE2_BIT = 1u << 31;

bool vis_is_terrain(uvec2 v) { return (v.x & VIS_TERRAIN_BIT) != 0u; }
uint vis_cluster_ref(uvec2 v) { return v.x; }
uint vis_terrain_patch(uvec2 v) { return v.x & ~VIS_TERRAIN_BIT; }
uint vis_triangle(uvec2 v) { return v.y & ~VIS_PHASE2_BIT; }
uint vis_phase(uvec2 v) { return v.y >> 31; }

uvec2 vis_encode_cluster(uint ref_idx, uint tri, uint phase) { return uvec2(ref_idx, tri | (phase << 31)); }
uvec2 vis_encode_terrain(uint patch_idx, uint tri, uint phase) { return uvec2(patch_idx | VIS_TERRAIN_BIT, tri | (phase << 31)); }

#endif