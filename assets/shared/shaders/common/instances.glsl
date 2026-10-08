#ifndef INSTANCES_GLSL
#define INSTANCES_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Instance {
    uint mesh_id;
    uint transform_id;
    uint entity_id;
    uint _pad0;
};

struct Transform {
    mat4 prev_mtx;
    mat4 curr_mtx;
};

layout(buffer_reference, scalar) readonly buffer InstanceBuffer { Instance data[]; };
layout(buffer_reference, scalar) readonly buffer TransformBuffer { Transform data[]; };

float transform_max_scale(mat4 m) {
    return max(length(m[0].xyz), max(length(m[1].xyz), length(m[2].xyz)));
}

#endif