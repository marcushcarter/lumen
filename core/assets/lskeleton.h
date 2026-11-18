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

struct SkeletonChain
{
    int32_t root = SKELETON_NO_JOINT;
    float stiffness = 0.05f;
    float damping = 0.1f;
    float inertia = 0.6f;
    float radius = 0.03f;
    float angle_limit = 70.0f;

    bool operator==(const SkeletonChain&) const = default;
};

inline constexpr uint32_t SKELETON_CLOTH_NO_SLOT = 0xFFFFFFFFu;

struct SkeletonCloth
{
    uint32_t slot = SKELETON_CLOTH_NO_SLOT;
    uint32_t resolution = 64;
    float stretch = 1.0f;
    float bend = 0.3f;
    float damping = 0.05f;
    float inertia = 0.6f;
    float thickness = 0.01f;
    float max_distance = 0.5f;

    bool operator==(const SkeletonCloth&) const = default;
};

struct SkeletonCollider
{
    int32_t joint = SKELETON_NO_JOINT;
    vec3 a = vec3(0.0f);
    vec3 b = vec3(0.0f);
    float radius = 0.1f;

    bool operator==(const SkeletonCollider&) const = default;
};

static_assert(sizeof(SkeletonCollider) == 32, "SkeletonCollider layout changed");
static_assert(std::is_trivially_copyable_v<SkeletonChain> && std::is_trivially_copyable_v<SkeletonCloth> && std::is_trivially_copyable_v<SkeletonCollider>, "must be blittable");

inline constexpr uint32_t SKELETON_DYNAMICS_MAGIC = 0x344E5944u;
inline constexpr uint32_t SKELETON_DYNAMICS_MAGIC_V3 = 0x334E5944u;
inline constexpr uint32_t SKELETON_DYNAMICS_MAGIC_V2 = 0x324E5944u;

struct LSkeletonDynamicsHeader
{
    uint32_t magic;
    uint32_t chain_count;
    uint32_t cloth_count;
    uint32_t collider_count;
};

static_assert(sizeof(LSkeletonDynamicsHeader) == 16, "LSkeletonDynamicsHeader layout changed");

struct LSkeleton
{
    Guid guid;
    std::vector<std::string> names;
    std::vector<int32_t> parents;
    std::vector<JointTransform> rest;
    std::vector<SkeletonChain> chains;
    std::vector<SkeletonCloth> cloths;
    std::vector<SkeletonCollider> colliders;

    uint32_t joint_count() const { return (uint32_t)names.size(); }
    int32_t find(std::string_view p_name) const;
    uint64_t fingerprint() const;
    void globals(const JointTransform* p_local, mat4* r_global) const;
    void branch(int32_t p_root, std::vector<uint8_t>& r_in_branch) const;

    Error load_cooked(const std::filesystem::path& p_path);
    Error save_cooked(const std::filesystem::path& p_path) const;
    Error encode(std::vector<uint8_t>& r_payload) const;
    Error decode(const std::vector<uint8_t>& p_payload);
};

}
