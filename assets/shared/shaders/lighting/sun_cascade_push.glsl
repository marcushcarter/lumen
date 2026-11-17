#ifndef SUN_CASCADE_PUSH_GLSL
#define SUN_CASCADE_PUSH_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

#include "common/geometry.glsl"
#include "common/camera.glsl"
#include "common/instances.glsl"
#include "lighting/sun_cascade_common.glsl"

layout(push_constant, scalar) uniform Push {
    CameraBuffer camera;
    GeometryBuffer geometry;
    InstanceBuffer instances;
    TransformBuffer transforms;
    SunCascadeListBuffer list;
    DirectLightBuffer light;
    SunCascadeMaterialBuffer materials;
    SunCascadeSpillBuffer spill;
    uint cascade;
    uint sampler_index;
    float mip_bias;
    uint kind;
} pc;

#endif
