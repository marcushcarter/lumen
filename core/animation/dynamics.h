#pragma once
#include <core/assets/lmesh.h>
#include <core/assets/lskeleton.h>
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

struct DynamicsBox
{
    vec3 center = vec3(0.0f);
    vec3 axis[3] = { vec3(1.0f, 0.0f, 0.0f), vec3(0.0f, 1.0f, 0.0f), vec3(0.0f, 0.0f, 1.0f) };
    vec3 half = vec3(0.0f);
};

struct DynamicsSource
{
    vec3 center = vec3(0.0f);
    float radius = 0.0f;
    uint32_t owner = UINT32_MAX;
    uint32_t capsule_first = 0;
    uint32_t capsule_count = 0;
    uint32_t box_first = 0;
    uint32_t box_count = 0;
};

struct DynamicsWorld
{
    std::vector<DynamicsSource> sources;
    std::vector<DynamicsCollider> capsules;
    std::vector<DynamicsBox> boxes;

    void clear();
    void begin(uint32_t p_owner);
    void end();
    void capsule(const vec3& p_a, const vec3& p_b, float p_radius);
    void box(const DynamicsBox& p_box);
};

struct DynamicsContacts
{
    struct Capsule {
        vec3 a;
        vec3 ab;
        vec3 mid;
        float inv_len2;
        float reach;
        float reach2;
        float bound2;
    };
    struct Box {
        vec3 center;
        vec3 axis[3];
        vec3 extent;
        float bound2;
    };
    std::vector<Capsule> capsules;
    std::vector<Box> boxes;

    bool empty() const { return capsules.empty() && boxes.empty(); }
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
    DynamicsContacts contacts;
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
    DynamicsContacts contacts;
    std::vector<vec3> local;
    std::vector<float> reach;
};

struct CharacterDynamics
{
    uint64_t key = 0;
    bool initialized = false;
    vec3 origin = vec3(0.0f);
    mat4 frame = mat4(1.0f);
    float chain_time = 0.0f;
    float cloth_time = 0.0f;
    float lod_time = 0.0f;
    uint32_t lod_frame = 0;
    uint32_t step_limit = 2;
    std::vector<DynamicsCollider> colliders;
    std::vector<DynamicsBox> boxes;
    std::vector<uint8_t> moving;
    std::vector<DynamicsChain> chains;
    std::vector<DynamicsCloth> cloths;

    void bind(uint64_t p_key, const LSkeleton& p_skeleton, const LMeshRig& p_rig);
    void update_colliders(const LSkeleton& p_skeleton, const std::vector<mat4>& p_global, const mat4& p_model);
    void gather_world(const DynamicsWorld& p_world, uint32_t p_owner, const vec3& p_center, float p_radius, float p_margin);
    void simulate_chains(const LSkeleton& p_skeleton, const mat4& p_model, float p_dt, float p_gravity, JointTransform* r_local, std::vector<mat4>& r_global);
    void simulate_cloth(const LSkeleton& p_skeleton, const LMeshRig& p_rig, const mat4& p_model, float p_dt, float p_gravity, bool p_solve, std::vector<mat4>& r_skin);
    void hold(const mat4& p_model);
    void finish(const mat4& p_model);
    void follow_chains(const mat4& p_model, const std::vector<mat4>& p_global);
    void draw(DebugDraw& r_draw, bool p_chains, bool p_cloth) const;
};

float dynamics_reach(const LSkeleton& p_skeleton);

struct DynamicsComponent
{
    CharacterDynamics dynamics;
};

}
