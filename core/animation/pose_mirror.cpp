#include <core/animation/pose_mirror.h>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <string_view>
#include <utility>

namespace lumen {

static std::string _mirror_name(const std::string& p_name)
{
    static constexpr std::pair<std::string_view, std::string_view> SUFFIXES[] = { { "_l", "_r" }, { "_L", "_R" }, { ".l", ".r" }, { ".L", ".R" } };
    static constexpr std::pair<std::string_view, std::string_view> PREFIXES[] = { { "l_", "r_" }, { "L_", "R_" } };
    static constexpr std::pair<std::string_view, std::string_view> WORDS[] = { { "Left", "Right" }, { "left", "right" }, { "LEFT", "RIGHT" } };
    const std::string_view name = p_name;
    for (const auto& [a, b] : SUFFIXES) {
        if (name.ends_with(a)) return std::string(name.substr(0, name.size() - a.size())) + std::string(b);
        if (name.ends_with(b)) return std::string(name.substr(0, name.size() - b.size())) + std::string(a);
    }
    for (const auto& [a, b] : PREFIXES) {
        if (name.starts_with(a)) return std::string(b) + std::string(name.substr(a.size()));
        if (name.starts_with(b)) return std::string(a) + std::string(name.substr(b.size()));
    }
    for (const auto& [a, b] : WORDS) {
        size_t at = name.find(a);
        if (at != std::string_view::npos) return std::string(name.substr(0, at)) + std::string(b) + std::string(name.substr(at + a.size()));
        at = name.find(b);
        if (at != std::string_view::npos) return std::string(name.substr(0, at)) + std::string(a) + std::string(name.substr(at + b.size()));
    }
    return {};
}

void PoseMirror::bind(const LSkeleton& p_skeleton)
{
    const uint32_t n = p_skeleton.joint_count();
    if (skeleton == p_skeleton.guid && p_skeleton.guid.valid() && joints == n) return;
    skeleton = p_skeleton.guid;
    joints = n;
    pair.resize(n);
    correction.resize(n);
    _global.resize(n);
    _mirrored.resize(n);
    if (n == 0) return;

    for (uint32_t j = 0; j < n; j++) {
        pair[j] = (int32_t)j;
        const std::string other = _mirror_name(p_skeleton.names[j]);
        if (other.empty()) continue;
        const int32_t k = p_skeleton.find(other);
        if (k >= 0) pair[j] = k;
    }
    for (uint32_t j = 0; j < n; j++) {
        if (pair[pair[j]] != (int32_t)j) pair[j] = (int32_t)j;
    }

    p_skeleton.globals(p_skeleton.rest.data(), _global.data());
    vec3 lateral(0.0f);
    for (uint32_t j = 0; j < n; j++) {
        if (pair[j] <= (int32_t)j) continue;
        vec3 d = vec3(_global[j][3]) - vec3(_global[pair[j]][3]);
        if (dot(d, lateral) < 0.0f) d = -d;
        lateral += d;
    }
    lateral = length(lateral) > 1.0e-5f ? normalize(lateral) : vec3(1.0f, 0.0f, 0.0f);
    reflect = mat4(mat3(1.0f) - 2.0f * outerProduct(lateral, lateral));
    for (uint32_t j = 0; j < n; j++) correction[j] = inverse(reflect * _global[pair[j]]) * _global[j];
}

void PoseMirror::apply(const LSkeleton& p_skeleton, JointTransform* r_local)
{
    const uint32_t n = p_skeleton.joint_count();
    if (n == 0 || joints != n) return;
    p_skeleton.globals(r_local, _global.data());
    for (uint32_t j = 0; j < n; j++) _mirrored[j] = reflect * _global[pair[j]] * correction[j];
    for (uint32_t j = 0; j < n; j++) {
        const int32_t parent = p_skeleton.parents[j];
        const mat4 local = parent >= 0 ? inverse(_mirrored[parent]) * _mirrored[j] : _mirrored[j];
        const vec3 x(local[0]);
        const vec3 y(local[1]);
        const vec3 z(local[2]);
        const vec3 scale = max(vec3(length(x), length(y), length(z)), vec3(1.0e-8f));
        r_local[j].translation = vec3(local[3]);
        r_local[j].scale = scale;
        r_local[j].rotation = normalize(quat_cast(mat3(x / scale.x, y / scale.y, z / scale.z)));
    }
}

}
