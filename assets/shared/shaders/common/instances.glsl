#ifndef INSTANCES_GLSL
#define INSTANCES_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

const uint SKIN_NONE = 0xFFFFFFFFu;
const uint INSTANCE_FLAG_PLAYER = 1u << 0u;

struct Instance {
    uint mesh_id;
    uint transform_id;
    uint entity_id;
    uint slot_base;
    uint skin_id;
    uint flags;
    uint tint;
};

struct SkinInstance {
    vec3 box_min;
    uint joint_base;
    vec3 box_extent;
    uint joint_count;
};

const uint SKIN_VIEW_SPLIT = 0x80000000u;

uint skin_joints(SkinInstance p_skin) {
    return p_skin.joint_count & ~SKIN_VIEW_SPLIT;
}

bool skin_view_split(SkinInstance p_skin) {
    return (p_skin.joint_count & SKIN_VIEW_SPLIT) != 0u;
}

uint skin_view_base(SkinInstance p_skin) {
    return p_skin.joint_base + (skin_view_split(p_skin) ? skin_joints(p_skin) : 0u);
}

struct Transform {
    mat4 prev_mtx;
    mat4 curr_mtx;
};

layout(buffer_reference, scalar) readonly buffer InstanceBuffer { Instance data[]; };
layout(buffer_reference, scalar) readonly buffer TransformBuffer { Transform data[]; };
layout(buffer_reference, scalar) readonly buffer SkinInstanceBuffer { SkinInstance data[]; };
layout(buffer_reference, scalar, buffer_reference_align = 16) buffer JointBuffer { vec4 data[]; };

float transform_max_scale(mat4 m) {
    return max(length(m[0].xyz), max(length(m[1].xyz), length(m[2].xyz)));
}

#endif