#include <core/animation/pose_blend.h>

namespace lumen {

static constexpr float WEIGHT_EPSILON = 1.0e-5f;
static const quat QUAT_IDENTITY = quat(1.0f, 0.0f, 0.0f, 0.0f);

void pose_blend(const PoseInput* p_inputs, uint32_t p_count, uint32_t p_joints, JointTransform* r_out)
{
    float total = 0.0f;
    const JointTransform* first = nullptr;
    for (uint32_t i = 0; i < p_count; i++) {
        if (p_inputs[i].weight <= WEIGHT_EPSILON) continue;
        total += p_inputs[i].weight;
        if (!first) first = p_inputs[i].pose;
    }
    if (!first) return;

    for (uint32_t j = 0; j < p_joints; j++) {
        vec3 t(0.0f);
        vec3 s(0.0f);
        quat r(0.0f, 0.0f, 0.0f, 0.0f);
        const quat anchor = first[j].rotation;
        for (uint32_t i = 0; i < p_count; i++) {
            if (p_inputs[i].weight <= WEIGHT_EPSILON) continue;
            const float w = p_inputs[i].weight / total;
            const JointTransform& x = p_inputs[i].pose[j];
            t += x.translation * w;
            s += x.scale * w;
            r += (dot(anchor, x.rotation) < 0.0f ? -x.rotation : x.rotation) * w;
        }
        const float len = length(r);
        r_out[j].translation = t;
        r_out[j].scale = s;
        r_out[j].rotation = len > WEIGHT_EPSILON ? r / len : anchor;
    }
}

void pose_layer(JointTransform* r_pose, const JointTransform* p_target, float p_weight, const float* p_joint_weights, uint32_t p_joints)
{
    for (uint32_t j = 0; j < p_joints; j++) {
        const float w = p_weight * (p_joint_weights ? p_joint_weights[j] : 1.0f);
        if (w <= WEIGHT_EPSILON) continue;
        JointTransform& x = r_pose[j];
        x.translation = mix(x.translation, p_target[j].translation, w);
        x.scale = mix(x.scale, p_target[j].scale, w);
        x.rotation = slerp(x.rotation, p_target[j].rotation, w);
    }
}

void pose_additive_delta(const JointTransform* p_pose, const JointTransform* p_reference, uint32_t p_joints, JointTransform* r_delta)
{
    for (uint32_t j = 0; j < p_joints; j++) {
        const JointTransform& p = p_pose[j];
        const JointTransform& ref = p_reference[j];
        r_delta[j].translation = p.translation - ref.translation;
        r_delta[j].rotation = normalize(inverse(ref.rotation) * p.rotation);
        r_delta[j].scale = p.scale / max(ref.scale, vec3(WEIGHT_EPSILON));
    }
}

void pose_add(JointTransform* r_pose, const JointTransform* p_delta, float p_weight, const float* p_joint_weights, uint32_t p_joints)
{
    for (uint32_t j = 0; j < p_joints; j++) {
        const float w = p_weight * (p_joint_weights ? p_joint_weights[j] : 1.0f);
        if (w <= WEIGHT_EPSILON) continue;
        JointTransform& x = r_pose[j];
        x.translation += p_delta[j].translation * w;
        x.rotation = normalize(x.rotation * slerp(QUAT_IDENTITY, p_delta[j].rotation, w));
        x.scale *= mix(vec3(1.0f), p_delta[j].scale, w);
    }
}

}
