#include <core/animation/two_bone_ik.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

static constexpr float IK_EPSILON = 1.0e-4f;

static quat _rotation(const mat4& p_m)
{
    mat3 r(p_m);
    for (int c = 0; c < 3; ++c) {
        const float len = length(r[c]);
        if (len > 1e-6f) r[c] /= len;
    }
    return normalize(quat_cast(r));
}

static float _angle(const vec3& p_a, const vec3& p_b)
{
    return std::acos(std::clamp(dot(p_a, p_b), -1.0f, 1.0f));
}

static vec3 _axis(const vec3& p_a, const vec3& p_b, const vec3& p_fallback)
{
    const vec3 c = cross(p_a, p_b);
    const float len = length(c);
    if (len > IK_EPSILON) return c / len;
    const vec3 f = cross(p_a, p_fallback);
    const float flen = length(f);
    return flen > IK_EPSILON ? f / flen : vec3(1.0f, 0.0f, 0.0f);
}

TwoBoneChain two_bone_chain(const LSkeleton& p_skeleton, int32_t p_end)
{
    TwoBoneChain chain;
    if (p_end < 0 || p_end >= (int32_t)p_skeleton.joint_count()) return chain;
    const int32_t mid = p_skeleton.parents[p_end];
    if (mid < 0) return chain;
    const int32_t upper = p_skeleton.parents[mid];
    if (upper < 0) return chain;
    chain.upper = upper;
    chain.mid = mid;
    chain.end = p_end;
    return chain;
}

bool two_bone_ik(const LSkeleton& p_skeleton, std::vector<JointTransform>& r_local, std::vector<mat4>& r_globals, const TwoBoneChain& p_chain, const vec3& p_target, const vec3& p_pole, bool p_keep_end_rotation)
{
    if (!p_chain.valid() || r_local.size() != p_skeleton.joint_count()) return false;
    r_globals.resize(r_local.size());
    p_skeleton.globals(r_local.data(), r_globals.data());

    const vec3 a = vec3(r_globals[p_chain.upper][3]);
    const vec3 b = vec3(r_globals[p_chain.mid][3]);
    const vec3 c = vec3(r_globals[p_chain.end][3]);
    const quat a_gr = _rotation(r_globals[p_chain.upper]);
    const quat b_gr = _rotation(r_globals[p_chain.mid]);
    const quat end_gr = _rotation(r_globals[p_chain.end]);

    const float lab = length(b - a);
    const float lcb = length(b - c);
    if (lab < IK_EPSILON || lcb < IK_EPSILON || length(c - a) < IK_EPSILON) return false;
    const float lat = std::clamp(length(p_target - a), IK_EPSILON, lab + lcb - IK_EPSILON);

    const vec3 ac = normalize(c - a);
    const vec3 ab = normalize(b - a);
    const vec3 ba = -ab;
    const vec3 bc = normalize(c - b);
    const vec3 at = length(p_target - a) > IK_EPSILON ? normalize(p_target - a) : ac;

    const float ac_ab_0 = _angle(ac, ab);
    const float ba_bc_0 = _angle(ba, bc);
    const float ac_at_0 = _angle(ac, at);
    const float ac_ab_1 = std::acos(std::clamp((lcb * lcb - lab * lab - lat * lat) / (-2.0f * lab * lat), -1.0f, 1.0f));
    const float ba_bc_1 = std::acos(std::clamp((lat * lat - lab * lab - lcb * lcb) / (-2.0f * lab * lcb), -1.0f, 1.0f));

    const vec3 axis0 = _axis(c - a, b - a, p_pole);
    const vec3 axis1 = _axis(c - a, p_target - a, axis0);

    const quat r0 = angleAxis(ac_ab_1 - ac_ab_0, normalize(inverse(a_gr) * axis0));
    const quat r1 = angleAxis(ba_bc_1 - ba_bc_0, normalize(inverse(b_gr) * axis0));
    const quat r2 = angleAxis(ac_at_0, normalize(inverse(a_gr) * axis1));

    r_local[p_chain.upper].rotation = normalize(r_local[p_chain.upper].rotation * r0 * r2);
    r_local[p_chain.mid].rotation = normalize(r_local[p_chain.mid].rotation * r1);

    if (p_keep_end_rotation) {
        p_skeleton.globals(r_local.data(), r_globals.data());
        const quat parent = _rotation(r_globals[p_chain.mid]);
        r_local[p_chain.end].rotation = normalize(inverse(parent) * end_gr);
    }
    return true;
}

}
