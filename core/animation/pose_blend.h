#pragma once
#include <core/assets/lskeleton.h>
#include <cstdint>

namespace lumen {

struct PoseInput {
    const JointTransform* pose = nullptr;
    float weight = 0.0f;
};

void pose_blend(const PoseInput* p_inputs, uint32_t p_count, uint32_t p_joints, JointTransform* r_out);
void pose_layer(JointTransform* r_pose, const JointTransform* p_target, float p_weight, const float* p_joint_weights, uint32_t p_joints);
void pose_additive_delta(const JointTransform* p_pose, const JointTransform* p_reference, uint32_t p_joints, JointTransform* r_delta);
void pose_add(JointTransform* r_pose, const JointTransform* p_delta, float p_weight, const float* p_joint_weights, uint32_t p_joints);

}
