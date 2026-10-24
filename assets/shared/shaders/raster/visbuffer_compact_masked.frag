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
    CompactPrimBuffer prims;
    CompactDrawArgs args;
    CompactMaterialBuffer materials;
    uint phase;
    uint bin;
    uint sampler_index;
    uint material_base;
} pc;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out uint o_vis;

void main() {
    const uint o = pc.args.cmd[pc.phase * RASTER_BIN_COUNT + pc.bin].first_index / 3u + uint(gl_PrimitiveID);
    if (raster_alpha_clipped(pc.geometry, pc.materials.data[o - pc.material_base], v_uv, pc.sampler_index)) discard;
    o_vis = pc.prims.data[o];
}