#pragma once
#include <cstdint>
#include <glm/glm.hpp>

namespace lumen {
    
using namespace glm;

static constexpr uint32_t MAX_INSTANCES = 64u * 1024;

struct CameraUniform {
    mat4 prev_view_proj;
    mat4 curr_view_proj;
    vec4 position;
    vec4 frustum_planes[6];
    float near_z;
    float far_z;
};

struct Instance {
    uint32_t mesh_id;
    uint32_t transform_id;
    uint32_t _pad0[2];
};

struct Transform {
    mat4 prev_mtx;
    mat4 curr_mtx;
};

struct IndirectDispatch { uint32_t x, y, z; };

}