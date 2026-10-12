#ifndef VISBUFFER_GLSL
#define VISBUFFER_GLSL

const uint VIS_TERRAIN_BIT = 1u << 31;
const uint VIS_TRI_BITS = 8u;
const uint VIS_TRI_MASK = (1u << VIS_TRI_BITS) - 1u;
const uint VIS_EMPTY = 0xFFFFFFFFu;

bool vis_is_empty(uint v) { return v == VIS_EMPTY; }
bool vis_is_terrain(uint v) { return (v & VIS_TERRAIN_BIT) != 0u; }
uint vis_cluster_ref(uint v) { return v >> VIS_TRI_BITS; }
uint vis_terrain_patch(uint v) { return (v & ~VIS_TERRAIN_BIT) >> VIS_TRI_BITS; }
uint vis_triangle(uint v) { return v & VIS_TRI_MASK; }

uint vis_encode_cluster(uint ref_idx, uint tri) { return (ref_idx << VIS_TRI_BITS) | tri; }
uint vis_encode_terrain(uint patch_idx, uint tri) { return VIS_TERRAIN_BIT | (patch_idx << VIS_TRI_BITS) | tri; }

#endif