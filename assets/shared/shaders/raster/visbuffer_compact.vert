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

void main() {
    const uint slot = uint(gl_VertexIndex) >> COMPACT_LOCAL_BITS;
    const uvec2 ref = pc.cluster_refs.data[pc.visible.data[slot]];
    vec2 uv;
    gl_Position = raster_vertex(pc.camera, pc.geometry, pc.instances, pc.transforms, ref, uint(gl_VertexIndex) & COMPACT_LOCAL_MASK, uv);
}