#version 460

#include "raster/raster_common.glsl"
#include "raster/raster_masked.glsl"

layout(push_constant) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    ClusterRefsBuffer cluster_refs;
    VisibleClusterBuffer visible;
    uint bin;
    uint sampler_index;
} pc;

layout(location = 0) flat in uint v_vis;
layout(location = 1) flat in uint v_material;
layout(location = 2) in vec2 v_uv;
layout(location = 0) out uint o_vis;

void main() {
    if (raster_alpha_clipped(pc.geometry, v_material, v_uv, pc.sampler_index)) discard;
    o_vis = v_vis;
}