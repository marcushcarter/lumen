#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

namespace lumen {

using namespace glm;

inline constexpr uint32_t SKELETON_MAX_JOINTS = 256;
inline constexpr int32_t SKELETON_NO_JOINT = -1;

struct LSkeletonPayloadHeader
{
    uint32_t joint_count;
    uint32_t name_bytes;
};

static_assert(sizeof(LSkeletonPayloadHeader) == 8, "LSkeletonPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LSkeletonPayloadHeader>, "must be blittable");

struct LSkeletonJoint
{
    mat4 inverse_bind;
    quat rotation;
    vec3 translation;
    int32_t parent;
    vec3 scale;
    uint32_t name_offset;
};

static_assert(sizeof(LSkeletonJoint) == 112, "LSkeletonJoint layout changed");
static_assert(std::is_trivially_copyable_v<LSkeletonJoint>, "must be blittable");

struct LSkeleton
{
    struct Joint {
        std::string name;
        int32_t parent = SKELETON_NO_JOINT;
        vec3 translation = vec3(0.0f);
        quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
        vec3 scale = vec3(1.0f);
        mat4 inverse_bind = mat4(1.0f);
    };

    Guid guid;
    std::vector<Joint> joints;

    int32_t find(std::string_view p_name) const;
    Error load_cooked(const std::filesystem::path& p_path);
    Error save_cooked(const std::filesystem::path& p_path) const;
};

}
