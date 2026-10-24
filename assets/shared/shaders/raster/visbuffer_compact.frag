#version 460

#include "raster/raster_common.glsl"

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

layout(early_fragment_tests) in;

layout(location = 0) out uint o_vis;

void main() {
    o_vis = pc.prims.data[pc.args.cmd[pc.phase * RASTER_BIN_COUNT + pc.bin].first_index / 3u + uint(gl_PrimitiveID)];
}