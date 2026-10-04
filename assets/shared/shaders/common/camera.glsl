#ifndef CAMERA_GLSL
#define CAMERA_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

layout(buffer_reference, scalar) readonly buffer CameraBuffer {
    mat4 prev_view_proj;
    mat4 curr_view_proj;
    vec4 position;
    vec4 frustum_planes[6];
    float near_z;
    float far_z;
    float tan_half_fov_y;
};

bool frustum_cull_sphere(CameraBuffer p_camera, vec3 p_center, float p_radius) {
    for (int i = 0; i < 6; i++) {
        vec4 pl = p_camera.frustum_planes[i];
        if (dot(pl.xyz, p_center) + pl.w < -p_radius) return true;
    }
    return false;
}

#endif