#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "raster/raster_masked.glsl"
#include "editor/outline_mask_common.glsl"

layout(push_constant, scalar) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    OutlineMaskListBuffer list;
    OutlineMaskMaterialBuffer materials;
    uint sampler_index;
} pc;

layout(location = 0) in vec2 v_uv;

void main() {
    const uint material = pc.materials.data[uint(gl_PrimitiveID)];
    if (material != OUTLINE_MASK_OPAQUE && raster_alpha_clipped(pc.geometry, material, v_uv, pc.sampler_index)) discard;
}
