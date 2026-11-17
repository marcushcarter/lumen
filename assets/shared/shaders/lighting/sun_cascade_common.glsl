#ifndef SUN_CASCADE_COMMON_GLSL
#define SUN_CASCADE_COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/direct_light.glsl"

const uint SUN_CASCADE_OPAQUE = 0u;
const uint SUN_CASCADE_TWO_SIDED = 1u;
const uint SUN_CASCADE_MASKED = 2u;
const uint SUN_CASCADE_MIXED = 3u;
const uint SUN_CASCADE_KINDS = 4u;
const uint SUN_CASCADE_SLOT_SHIFT = 8u;
const uint SUN_CASCADE_LOCAL_MASK = (1u << SUN_CASCADE_SLOT_SHIFT) - 1u;
const uint SUN_CASCADE_SPILL_STRIDE = 128u * 1024u;
const vec4 SUN_CASCADE_REJECT = vec4(2.0, 2.0, 2.0, 1.0);

struct SunCascadeDraw {
    uint index_count;
    uint instance_count;
    uint first_index;
    int vertex_offset;
    uint first_instance;
};

struct SunCascadeSpillDraw {
    uint vertex_count;
    uint instance_count;
    uint first_vertex;
    uint first_instance;
};

layout(buffer_reference, scalar) buffer SunCascadeListBuffer {
    SunCascadeDraw draws[4];
    SunCascadeSpillDraw spill_draws[4];
    uvec3 compact_dispatch;
    uint cluster_count;
    uint capacity;
    uint tri_count[4];
    uint spill_count[4];
    uint _pad;
    uvec2 entries[];
};

layout(buffer_reference, scalar) buffer SunCascadeIndexBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer SunCascadeMaterialBuffer { uint data[]; };
layout(buffer_reference, scalar) buffer SunCascadeSpillBuffer { uint data[]; };
layout(buffer_reference, scalar) readonly buffer SunCascadeWorkBuffer { uvec2 data[]; };

vec4 sun_cascade_clip(DirectLightBuffer p_light, uint p_cascade, vec3 p_world) {
    vec4 p = p_light.cascade_view_proj[p_cascade] * vec4(p_world, 1.0);
    p.z = max(p.z, 0.0);
    return p;
}

#endif
