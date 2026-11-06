#version 460

#include "raster/raster_common.glsl"
#include "common/direct_light.glsl"

layout(buffer_reference, scalar) readonly buffer ShadowClusterList { uvec4 data[]; };

layout(push_constant, scalar) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    DirectLightBuffer direct_light;
    ShadowClusterList clusters;
    uint cascade;
    uint cluster_capacity;
} pc;

void main() {
    const uint slot = uint(gl_VertexIndex) >> 8u;
    if (slot >= pc.cluster_capacity) {
        gl_Position = RASTER_CLIP_REJECT;
        return;
    }
    const uvec2 ref = pc.clusters.data[pc.cascade * pc.cluster_capacity + slot].xy;
    vec2 uv;
    const vec3 world = raster_world(pc.camera, pc.geometry, pc.instances, pc.transforms, ref, uint(gl_VertexIndex) & 0xFFu, uv);
    gl_Position = pc.direct_light.cascade_view_proj[pc.cascade] * vec4(world, 1.0);
}
