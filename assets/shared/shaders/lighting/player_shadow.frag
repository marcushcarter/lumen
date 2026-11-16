#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "common/direct_light.glsl"
#include "raster/raster_masked.glsl"
#include "lighting/player_shadow_common.glsl"

layout(push_constant, scalar) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    PlayerShadowListBuffer list;
    DirectLightBuffer direct_light;
    PlayerShadowMaterialBuffer materials;
    uvec2 player_table;
    uvec2 skin_cache;
    uint instance;
    uint sampler_index;
} pc;

layout(location = 0) in vec2 v_uv;

void main() {
    const uint material = pc.materials.data[pc.list.draws[PLAYER_SHADOW_DRAW_MASKED].first_index / 3u + uint(gl_PrimitiveID)];
    if (material != PLAYER_SHADOW_OPAQUE && raster_alpha_clipped(pc.geometry, material, v_uv, pc.sampler_index)) discard;
}
