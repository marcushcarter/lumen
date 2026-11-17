#pragma once
#include <core/assets/lmesh.h>
#include <core/assets/lskeleton.h>
#include <core/game/character_ragdoll.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace lumen {

using namespace glm;

struct DebugDraw;

struct DynamicsCollider
{
    vec3 a = vec3(0.0f);
    vec3 b = vec3(0.0f);
    float radius = 0.0f;
    int32_t joint = SKELETON_NO_JOINT;
};

struct DynamicsChain
{
    uint32_t settings = 0;
    std::vector<int32_t> joints;
    std::vector<int32_t> parent;
    std::vector<int32_t> child;
    std::vector<uint8_t> in_branch;
    std::vector<vec3> pos;
    std::vector<vec3> prev;
    std::vector<vec3> anim;
    std::vector<uint16_t> nearby;
};

struct DynamicsCloth
{
    uint32_t cloth = 0;
    int32_t settings = -1;
    std::vector<vec3> pos;
    std::vector<vec3> prev;
    std::vector<vec3> anchor;
    std::vector<mat3> rest_frame;
    std::vector<int32_t> neighbor;
    std::vector<uint32_t> tri_offset;
    std::vector<uint32_t> tri_list;
    std::vector<uint16_t> nearby;
    std::vector<vec3> local;
};

struct CharacterDynamics
{
    uint64_t key = 0;
    bool initialized = false;
    vec3 origin = vec3(0.0f);
    std::vector<DynamicsCollider> colliders;
    std::vector<uint8_t> moving;
    RagdollBody body;
    std::vector<DynamicsChain> chains;
    std::vector<DynamicsCloth> cloths;

    void bind(uint64_t p_key, const LSkeleton& p_skeleton, const LMeshRig& p_rig, const std::vector<int32_t>& p_map, const LLocomotion& p_body);
    void update_colliders(const std::vector<mat4>& p_global, const mat4& p_model, float p_model_yaw);
    void simulate_chains(const LSkeleton& p_skeleton, const mat4& p_model, float p_dt, float p_gravity, JointTransform* r_local, std::vector<mat4>& r_global);
    void simulate_cloth(const LSkeleton& p_skeleton, const LMeshRig& p_rig, const mat4& p_model, float p_dt, float p_gravity, bool p_solve, std::vector<mat4>& r_skin);
    void finish(const mat4& p_model);
    void follow_chains(const mat4& p_model, const std::vector<mat4>& p_global);
    void draw(DebugDraw& r_draw, bool p_chains, bool p_cloth) const;
};

struct DynamicsComponent
{
    CharacterDynamics dynamics;
};

}
