#ifndef INSTANCES_GLSL
#define INSTANCES_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint SKIN_NONE = 0xFFFFFFFFu;

struct Instance {
    uint mesh_id;
    uint transform_id;
    uint entity_id;
    uint slot_base;
    uint skin_id;
};

struct SkinInstance {
    vec3 box_min;
    uint joint_base;
    vec3 box_extent;
    uint joint_count;
};

struct Transform {
    mat4 prev_mtx;
    mat4 curr_mtx;
};

layout(buffer_reference, scalar) readonly buffer InstanceBuffer { Instance data[]; };
layout(buffer_reference, scalar) readonly buffer TransformBuffer { Transform data[]; };
layout(buffer_reference, scalar) readonly buffer SkinInstanceBuffer { SkinInstance data[]; };
layout(buffer_reference, scalar, buffer_reference_align = 16) readonly buffer JointBuffer { vec4 data[]; };

float transform_max_scale(mat4 m) {
    return max(length(m[0].xyz), max(length(m[1].xyz), length(m[2].xyz)));
}

#endif