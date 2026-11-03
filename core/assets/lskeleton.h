#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace lumen {

using namespace glm;

inline constexpr uint32_t SKELETON_MAX_JOINTS = 1024;
inline constexpr int32_t SKELETON_NO_JOINT = -1;

struct JointTransform
{
    vec3 translation = vec3(0.0f);
    quat rotation = quat(1.0f, 0.0f, 0.0f, 0.0f);
    vec3 scale = vec3(1.0f);

    mat4 matrix() const;
};

static_assert(sizeof(JointTransform) == 40, "JointTransform layout changed");
static_assert(std::is_trivially_copyable_v<JointTransform>, "must be blittable");

struct LSkeletonPayloadHeader
{
    uint64_t fingerprint;
    uint32_t joint_count;
    uint32_t name_bytes;
};

static_assert(sizeof(LSkeletonPayloadHeader) == 16, "LSkeletonPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LSkeletonPayloadHeader>, "must be blittable");

struct LSkeleton
{
    Guid guid;
    std::vector<std::string> names;
    std::vector<int32_t> parents;
    std::vector<JointTransform> rest;

    uint32_t joint_count() const { return (uint32_t)names.size(); }
    int32_t find(std::string_view p_name) const;
    uint64_t fingerprint() const;
    void globals(const JointTransform* p_local, mat4* r_global) const;

    Error load_cooked(const std::filesystem::path& p_path);
    Error save_cooked(const std::filesystem::path& p_path) const;
};

}
