#include <core/animation/dynamics.h>
#include <core/rendering/debug_draw.h>
#include <core/base/jobs.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cfloat>
#include <cmath>

namespace lumen {

static constexpr float DYNAMICS_STEP = 1.0f / 60.0f;
static constexpr uint32_t DYNAMICS_MAX_STEPS = 4;
static constexpr float DYNAMICS_MAX_DT = 1.0f / 15.0f;
static constexpr float DYNAMICS_TELEPORT = 2.0f;
static constexpr uint32_t CHAIN_ITERATIONS = 2;
static constexpr uint32_t CLOTH_ITERATIONS = 6;
static constexpr float DYNAMICS_EPSILON = 1e-6f;

static vec3 _safe_normalize(const vec3& p_v, const vec3& p_fallback)
{
    const float l = length(p_v);
    return l > DYNAMICS_EPSILON ? p_v / l : p_fallback;
}

static quat _from_to(const vec3& p_from, const vec3& p_to)
{
    const vec3 a = _safe_normalize(p_from, vec3(0.0f, 1.0f, 0.0f));
    const vec3 b = _safe_normalize(p_to, a);
    const float d = dot(a, b);
    if (d > 1.0f - 1e-6f) return quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d < -1.0f + 1e-6f) {
        vec3 axis = cross(vec3(1.0f, 0.0f, 0.0f), a);
        if (dot(axis, axis) < 1e-6f) axis = cross(vec3(0.0f, 1.0f, 0.0f), a);
        return angleAxis(glm::pi<float>(), normalize(axis));
    }
    const vec3 c = cross(a, b);
    return normalize(quat(1.0f + d, c.x, c.y, c.z));
}

static quat _rotation_of(const mat4& p_m)
{
    const mat3 r(_safe_normalize(vec3(p_m[0]), vec3(1.0f, 0.0f, 0.0f)), _safe_normalize(vec3(p_m[1]), vec3(0.0f, 1.0f, 0.0f)), _safe_normalize(vec3(p_m[2]), vec3(0.0f, 0.0f, 1.0f)));
    return normalize(quat_cast(r));
}

static float _model_scale(const mat4& p_model)
{
    return std::max(length(vec3(p_model[0])), DYNAMICS_EPSILON);
}

static vec3 _closest_on_segment(const vec3& p_p, const vec3& p_a, const vec3& p_b)
{
    const vec3 ab = p_b - p_a;
    const float len2 = dot(ab, ab);
    const float t = len2 > DYNAMICS_EPSILON ? std::clamp(dot(p_p - p_a, ab) / len2, 0.0f, 1.0f) : 0.0f;
    return p_a + ab * t;
}

static void _gather(const vec3& p_center, float p_radius, float p_particle, const std::vector<DynamicsCollider>& p_colliders, const std::vector<uint8_t>& p_skip, const std::vector<DynamicsBox>& p_boxes, DynamicsContacts& r_contacts)
{
    r_contacts.capsules.clear();
    r_contacts.boxes.clear();
    for (const DynamicsCollider& c : p_colliders) {
        if (c.radius <= 0.0f || (c.joint >= 0 && c.joint < (int32_t)p_skip.size() && p_skip[c.joint])) continue;
        const vec3 d = p_center - _closest_on_segment(p_center, c.a, c.b);
        const float reach = c.radius + p_particle;
        const float gate = p_radius + reach;
        if (dot(d, d) > gate * gate) continue;
        const vec3 ab = c.b - c.a;
        const float len2 = dot(ab, ab);
        const float bound = std::sqrt(len2) * 0.5f + reach;
        r_contacts.capsules.push_back(DynamicsContacts::Capsule{ c.a, ab, (c.a + c.b) * 0.5f, len2 > DYNAMICS_EPSILON ? 1.0f / len2 : 0.0f, reach, reach * reach, bound * bound });
    }
    for (const DynamicsBox& box : p_boxes) {
        const vec3 d = p_center - box.center;
        bool hit = true;
        for (int k = 0; k < 3 && hit; k++) hit = std::abs(dot(d, box.axis[k])) <= box.half[k] + p_particle + p_radius;
        if (!hit) continue;
        const float bound = length(box.half) + p_particle;
        r_contacts.boxes.push_back(DynamicsContacts::Box{ box.center, { box.axis[0], box.axis[1], box.axis[2] }, box.half + vec3(p_particle), bound * bound });
    }
}

static void _collide(vec3& r_p, const DynamicsContacts& p_contacts)
{
    float px = r_p.x, py = r_p.y, pz = r_p.z;
    for (const DynamicsContacts::Capsule& c : p_contacts.capsules) {
        const float mx = px - c.mid.x, my = py - c.mid.y, mz = pz - c.mid.z;
        if (mx * mx + my * my + mz * mz > c.bound2) continue;
        const float rx = px - c.a.x, ry = py - c.a.y, rz = pz - c.a.z;
        float t = (rx * c.ab.x + ry * c.ab.y + rz * c.ab.z) * c.inv_len2;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        const float cx = c.a.x + c.ab.x * t, cy = c.a.y + c.ab.y * t, cz = c.a.z + c.ab.z * t;
        const float dx = px - cx, dy = py - cy, dz = pz - cz;
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 >= c.reach2) continue;
        if (d2 > 1e-12f) {
            const float k = c.reach / std::sqrt(d2);
            px = cx + dx * k;
            py = cy + dy * k;
            pz = cz + dz * k;
        } else {
            py = cy + c.reach;
        }
    }
    for (const DynamicsContacts::Box& b : p_contacts.boxes) {
        const float dx = px - b.center.x, dy = py - b.center.y, dz = pz - b.center.z;
        if (dx * dx + dy * dy + dz * dz > b.bound2) continue;
        float best = FLT_MAX, push = 0.0f;
        int axis = -1;
        for (int k = 0; k < 3; k++) {
            const vec3& n = b.axis[k];
            const float x = dx * n.x + dy * n.y + dz * n.z;
            const float depth = b.extent[k] - (x < 0.0f ? -x : x);
            if (depth <= 0.0f) {
                axis = -1;
                break;
            }
            if (depth < best) {
                best = depth;
                axis = k;
                push = x < 0.0f ? -depth : depth;
            }
        }
        if (axis < 0) continue;
        px += b.axis[axis].x * push;
        py += b.axis[axis].y * push;
        pz += b.axis[axis].z * push;
    }
    r_p.x = px;
    r_p.y = py;
    r_p.z = pz;
}

void DynamicsWorld::clear()
{
    sources.clear();
    capsules.clear();
    boxes.clear();
}

void DynamicsWorld::begin(uint32_t p_owner)
{
    DynamicsSource& src = sources.emplace_back();
    src.owner = p_owner;
    src.capsule_first = (uint32_t)capsules.size();
    src.box_first = (uint32_t)boxes.size();
}

void DynamicsWorld::capsule(const vec3& p_a, const vec3& p_b, float p_radius)
{
    if (p_radius > 0.0f) capsules.push_back(DynamicsCollider{ p_a, p_b, p_radius, SKELETON_NO_JOINT });
}

void DynamicsWorld::box(const DynamicsBox& p_box)
{
    boxes.push_back(p_box);
}

void DynamicsWorld::end()
{
    DynamicsSource& src = sources.back();
    src.capsule_count = (uint32_t)capsules.size() - src.capsule_first;
    src.box_count = (uint32_t)boxes.size() - src.box_first;
    if (src.capsule_count + src.box_count == 0) {
        sources.pop_back();
        return;
    }
    vec3 lo(FLT_MAX), hi(-FLT_MAX);
    for (uint32_t i = 0; i < src.capsule_count; i++) {
        const DynamicsCollider& c = capsules[src.capsule_first + i];
        lo = glm::min(lo, glm::min(c.a, c.b) - vec3(c.radius));
        hi = glm::max(hi, glm::max(c.a, c.b) + vec3(c.radius));
    }
    for (uint32_t i = 0; i < src.box_count; i++) {
        const DynamicsBox& box = boxes[src.box_first + i];
        const vec3 e = glm::abs(box.axis[0]) * box.half.x + glm::abs(box.axis[1]) * box.half.y + glm::abs(box.axis[2]) * box.half.z;
        lo = glm::min(lo, box.center - e);
        hi = glm::max(hi, box.center + e);
    }
    src.center = (lo + hi) * 0.5f;
    src.radius = length(hi - lo) * 0.5f;
}

float dynamics_reach(const LSkeleton& p_skeleton)
{
    float reach = 0.0f;
    for (const SkeletonCloth& c : p_skeleton.cloths) reach = std::max(reach, std::max(c.max_distance, 0.0f) + c.thickness);
    for (const SkeletonChain& c : p_skeleton.chains) reach = std::max(reach, c.radius);
    return reach;
}

static mat3 _frame(const vec3& p_normal, const vec3& p_toward)
{
    const vec3 n = _safe_normalize(p_normal, vec3(0.0f, 1.0f, 0.0f));
    vec3 t = p_toward - n * dot(p_toward, n);
    t = _safe_normalize(t, std::abs(n.x) < 0.9f ? normalize(cross(n, vec3(1.0f, 0.0f, 0.0f))) : normalize(cross(n, vec3(0.0f, 1.0f, 0.0f))));
    return mat3(t, cross(n, t), n);
}

static uint32_t _substeps(float p_dt, uint32_t p_limit, float& r_time, float& r_alpha)
{
    r_time += std::clamp(p_dt, 0.0f, DYNAMICS_MAX_DT);
    const uint32_t steps = std::min((uint32_t)(r_time / DYNAMICS_STEP), std::clamp(p_limit, 1u, DYNAMICS_MAX_STEPS));
    r_time = std::clamp(r_time - (float)steps * DYNAMICS_STEP, 0.0f, DYNAMICS_STEP);
    r_alpha = r_time / DYNAMICS_STEP;
    return steps;
}

void CharacterDynamics::bind(uint64_t p_key, const LSkeleton& p_skeleton, const LMeshRig& p_rig)
{
    if (key == p_key) return;
    key = p_key;
    initialized = false;
    colliders.clear();
    boxes.clear();
    chains.clear();
    cloths.clear();
    moving.assign(p_skeleton.joint_count(), 0);
    std::vector<uint8_t> branch;
    auto mark = [&](int32_t p_root) {
        if (p_root < 0 || p_root >= (int32_t)p_skeleton.joint_count()) return;
        p_skeleton.branch(p_root, branch);
        for (size_t j = 0; j < branch.size() && j < moving.size(); j++) moving[j] |= branch[j];
    };
    for (const SkeletonChain& c : p_skeleton.chains) mark(c.root);

    for (uint32_t c = 0; c < (uint32_t)p_skeleton.chains.size(); c++) {
        DynamicsChain chain;
        chain.settings = c;
        p_skeleton.branch(p_skeleton.chains[c].root, chain.in_branch);
        std::vector<int32_t> local_of(p_skeleton.joint_count(), -1);
        for (uint32_t j = 0; j < p_skeleton.joint_count(); j++) {
            if (!chain.in_branch[j]) continue;
            local_of[j] = (int32_t)chain.joints.size();
            chain.joints.push_back((int32_t)j);
            const int32_t parent = p_skeleton.parents[j];
            chain.parent.push_back(parent >= 0 && chain.in_branch[parent] ? local_of[parent] : -1);
            chain.child.push_back(-1);
            if (chain.parent.back() >= 0 && chain.child[chain.parent.back()] < 0) chain.child[chain.parent.back()] = local_of[j];
        }
        if (chain.joints.size() < 2) continue;
        chains.push_back(std::move(chain));
    }

    const LMeshDynamics& dyn = p_rig.dynamics;
    for (uint32_t i = 0; i < (uint32_t)dyn.cloths.size(); i++) {
        const LMeshCloth& src = dyn.cloths[i];
        DynamicsCloth cloth;
        cloth.cloth = i;
        for (uint32_t s = 0; src.root_joint == LMESH_CLOTH_NO_ROOT && s < (uint32_t)p_skeleton.cloths.size() && cloth.settings < 0; s++) {
            if (p_skeleton.cloths[s].slot == src.slot) cloth.settings = (int32_t)s;
        }

        const uint32_t n = src.particle_count;
        const LMeshClothParticle* parts = dyn.particles.data() + src.particle_first;
        std::vector<uint32_t> counts(n, 0u);
        for (uint32_t t = 0; t < src.triangle_count; t++) {
            const u16vec4 tri = dyn.triangles[src.triangle_first + t];
            counts[tri.x]++;
            counts[tri.y]++;
            counts[tri.z]++;
        }
        cloth.tri_offset.assign(n + 1, 0u);
        for (uint32_t p = 0; p < n; p++) cloth.tri_offset[p + 1] = cloth.tri_offset[p] + counts[p];
        cloth.tri_list.assign(cloth.tri_offset[n], 0u);
        std::vector<uint32_t> fill(cloth.tri_offset.begin(), cloth.tri_offset.end() - 1);
        for (uint32_t t = 0; t < src.triangle_count; t++) {
            const u16vec4 tri = dyn.triangles[src.triangle_first + t];
            cloth.tri_list[fill[tri.x]++] = t;
            cloth.tri_list[fill[tri.y]++] = t;
            cloth.tri_list[fill[tri.z]++] = t;
        }
        cloth.neighbor.assign(n, -1);
        for (uint32_t l = 0; l < src.stretch_count; l++) {
            const LMeshClothLink& link = dyn.links[src.link_first + l];
            if (cloth.neighbor[link.a] < 0) cloth.neighbor[link.a] = link.b;
            if (cloth.neighbor[link.b] < 0) cloth.neighbor[link.b] = link.a;
        }
        cloth.rest_frame.resize(n);
        for (uint32_t p = 0; p < n; p++) {
            vec3 normal(0.0f);
            for (uint32_t k = cloth.tri_offset[p]; k < cloth.tri_offset[p + 1]; k++) {
                const u16vec4 tri = dyn.triangles[src.triangle_first + cloth.tri_list[k]];
                normal += cross(parts[tri.y].rest - parts[tri.x].rest, parts[tri.z].rest - parts[tri.x].rest);
            }
            const vec3 toward = cloth.neighbor[p] >= 0 ? parts[cloth.neighbor[p]].rest - parts[p].rest : vec3(1.0f, 0.0f, 0.0f);
            cloth.rest_frame[p] = _frame(normal, toward);
        }
        cloths.push_back(std::move(cloth));
    }
}

void CharacterDynamics::update_colliders(const LSkeleton& p_skeleton, const std::vector<mat4>& p_global, const mat4& p_model)
{
    colliders.clear();
    if (chains.empty() && cloths.empty()) return;
    const float scale = _model_scale(p_model);
    for (const SkeletonCollider& c : p_skeleton.colliders) {
        if (c.joint < 0 || c.joint >= (int32_t)p_global.size()) continue;
        const mat4 m = p_model * p_global[c.joint];
        colliders.push_back(DynamicsCollider{ vec3(m * vec4(c.a, 1.0f)), vec3(m * vec4(c.b, 1.0f)), c.radius * scale, c.joint });
    }
}

void CharacterDynamics::gather_world(const DynamicsWorld& p_world, uint32_t p_owner, const vec3& p_center, float p_radius, float p_margin)
{
    boxes.clear();
    if (chains.empty() && cloths.empty()) return;
    vec3 center = p_center;
    float radius = p_radius;
    if (initialized) {
        vec3 lo(FLT_MAX), hi(-FLT_MAX);
        const auto grow = [&](const std::vector<vec3>& p_points) {
            for (const vec3& p : p_points) {
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            }
        };
        for (const DynamicsChain& chain : chains) grow(chain.pos);
        for (const DynamicsCloth& cloth : cloths) grow(cloth.pos);
        if (lo.x <= hi.x) {
            center = (lo + hi) * 0.5f;
            radius = length(hi - lo) * 0.5f + p_margin;
        }
    }
    for (const DynamicsSource& src : p_world.sources) {
        if (src.owner == p_owner) continue;
        const vec3 d = src.center - center;
        const float reach = src.radius + radius;
        if (dot(d, d) > reach * reach) continue;
        const uint32_t capsules = std::min(src.capsule_count, (uint32_t)UINT16_MAX - std::min((uint32_t)colliders.size(), (uint32_t)UINT16_MAX));
        colliders.insert(colliders.end(), p_world.capsules.begin() + src.capsule_first, p_world.capsules.begin() + src.capsule_first + capsules);
        const uint32_t count = std::min(src.box_count, (uint32_t)UINT16_MAX - std::min((uint32_t)boxes.size(), (uint32_t)UINT16_MAX));
        boxes.insert(boxes.end(), p_world.boxes.begin() + src.box_first, p_world.boxes.begin() + src.box_first + count);
    }
}

void CharacterDynamics::simulate_chains(const LSkeleton& p_skeleton, const mat4& p_model, float p_dt, float p_gravity, JointTransform* r_local, std::vector<mat4>& r_global)
{
    if (chains.empty()) return;
    const vec3 now = vec3(p_model[3]);
    const bool reset = !initialized || distance(now, origin) > DYNAMICS_TELEPORT;
    const vec3 moved = reset ? vec3(0.0f) : now - origin;
    const float step = DYNAMICS_STEP;
    float alpha = 0.0f;
    const uint32_t steps = _substeps(p_dt, step_limit, chain_time, alpha);
    const mat4 inv_model = inverse(p_model);

    for (DynamicsChain& chain : chains) {
        if (chain.settings >= p_skeleton.chains.size()) continue;
        const SkeletonChain& def = p_skeleton.chains[chain.settings];
        const size_t n = chain.joints.size();
        chain.anim.resize(n);
        for (size_t k = 0; k < n; k++) chain.anim[k] = vec3(p_model * r_global[chain.joints[k]][3]);
        if (reset || chain.pos.size() != n) {
            chain.pos = chain.anim;
            chain.prev = chain.anim;
        }

        const vec3 carry = moved * (1.0f - std::clamp(def.inertia, 0.0f, 1.0f));
        for (size_t k = 1; k < n; k++) {
            chain.pos[k] += carry;
            chain.prev[k] += carry;
        }

        const vec3 gravity = vec3(0.0f, -p_gravity, 0.0f) * step * step;
        const float damping = std::clamp(def.damping, 0.0f, 1.0f);
        const float pull = 1.0f - std::pow(1.0f - std::clamp(def.stiffness, 0.0f, 1.0f), step / DYNAMICS_STEP);
        const float limit = glm::radians(std::clamp(def.angle_limit, 0.0f, 180.0f));
        float extent = def.radius;
        for (size_t k = 1; k < n; k++) {
            if (chain.parent[k] >= 0) extent += length(chain.anim[k] - chain.anim[chain.parent[k]]);
        }
        _gather(chain.anim[0], extent, def.radius, colliders, moving, boxes, chain.contacts);
        for (uint32_t s = 0; s < steps; s++) {
            chain.pos[0] = chain.prev[0] = chain.anim[0];
            for (size_t k = 1; k < n; k++) {
                const vec3 velocity = (chain.pos[k] - chain.prev[k]) * (1.0f - damping);
                chain.prev[k] = chain.pos[k];
                chain.pos[k] += velocity + gravity;
                chain.pos[k] += (chain.anim[k] - chain.pos[k]) * pull;
            }
            for (uint32_t it = 0; it < CHAIN_ITERATIONS; it++) {
                for (size_t k = 1; k < n; k++) {
                    const int32_t p = chain.parent[k];
                    if (p < 0) continue;
                    const vec3 rest_dir = chain.anim[k] - chain.anim[p];
                    const float len = length(rest_dir);
                    const vec3 anim_dir = _safe_normalize(rest_dir, vec3(0.0f, -1.0f, 0.0f));
                    vec3 dir = _safe_normalize(chain.pos[k] - chain.pos[p], anim_dir);
                    const float angle = std::acos(std::clamp(dot(dir, anim_dir), -1.0f, 1.0f));
                    if (angle > limit) {
                        const vec3 axis = _safe_normalize(cross(anim_dir, dir), vec3(0.0f));
                        if (dot(axis, axis) > 0.5f) dir = angleAxis(limit, axis) * anim_dir;
                    }
                    chain.pos[k] = chain.pos[p] + dir * len;
                    if (!chain.contacts.empty()) _collide(chain.pos[k], chain.contacts);
                }
            }
            for (size_t k = 1; k < n; k++) {
                const int32_t p = chain.parent[k];
                if (p < 0) continue;
                const float len = length(chain.anim[k] - chain.anim[p]);
                chain.pos[k] = chain.pos[p] + _safe_normalize(chain.pos[k] - chain.pos[p], vec3(0.0f, -1.0f, 0.0f)) * len;
            }
        }

        for (size_t k = 0; k < n; k++) {
            const int32_t j = chain.joints[k];
            const int32_t parent = p_skeleton.parents[j];
            const mat4 parent_global = parent >= 0 ? r_global[parent] : mat4(1.0f);
            mat4 g = parent_global * r_local[j].matrix();
            const int32_t c = chain.child[k];
            if (c >= 0) {
                const int32_t cj = chain.joints[c];
                const vec3 origin_s = vec3(g[3]);
                const vec3 child_now = vec3(g * vec4(r_local[cj].translation, 1.0f));
                const vec3 target = vec3(inv_model * vec4(mix(chain.prev[c], chain.pos[c], alpha), 1.0f));
                const quat q = _from_to(child_now - origin_s, target - origin_s);
                const quat world_rot = normalize(q * _rotation_of(g));
                r_local[j].rotation = normalize(inverse(_rotation_of(parent_global)) * world_rot);
                g = parent_global * r_local[j].matrix();
            }
            r_global[j] = g;
        }
    }
}

void CharacterDynamics::simulate_cloth(const LSkeleton& p_skeleton, const LMeshRig& p_rig, const mat4& p_model, float p_dt, float p_gravity, bool p_solve, std::vector<mat4>& r_skin)
{
    if (cloths.empty()) return;
    const LMeshDynamics& dyn = p_rig.dynamics;
    const vec3 now = vec3(p_model[3]);
    const bool reset = !initialized || distance(now, origin) > DYNAMICS_TELEPORT;
    const vec3 moved = reset ? vec3(0.0f) : now - origin;
    const float scale = _model_scale(p_model);
    const mat4 inv_model = inverse(p_model);
    const float step = DYNAMICS_STEP;
    float alpha = 0.0f;
    const uint32_t steps = _substeps(p_dt, step_limit, cloth_time, alpha);

    const auto usable = [&](const DynamicsCloth& p_cloth) {
        return p_cloth.cloth < dyn.cloths.size() && dyn.cloths[p_cloth.cloth].joint_first + dyn.cloths[p_cloth.cloth].particle_count <= r_skin.size();
    };

    for (DynamicsCloth& cloth : cloths) {
        if (!usable(cloth)) continue;
        const LMeshCloth& src = dyn.cloths[cloth.cloth];
        const uint32_t n = src.particle_count;
        const LMeshClothParticle* parts = dyn.particles.data() + src.particle_first;
        cloth.anchor.resize(n);
        for (uint32_t p = 0; p < n; p++) {
            vec4 sum(0.0f);
            for (int k = 0; k < 4; k++) {
                const float w = (float)parts[p].weights[k] / 255.0f;
                if (w <= 0.0f || parts[p].joints[k] >= r_skin.size()) continue;
                sum += w * (r_skin[parts[p].joints[k]] * vec4(parts[p].rest, 1.0f));
            }
            cloth.anchor[p] = vec3(p_model * vec4(vec3(sum), 1.0f));
        }
        if (reset || cloth.pos.size() != n) {
            cloth.pos = cloth.anchor;
            cloth.prev = cloth.anchor;
        }
    }

    const auto solve = [&](uint32_t p_index) {
        DynamicsCloth& cloth = cloths[p_index];
        if (!usable(cloth)) return;
        const LMeshCloth& src = dyn.cloths[cloth.cloth];
        const uint32_t n = src.particle_count;
        const LMeshClothParticle* parts = dyn.particles.data() + src.particle_first;

        if (p_solve && cloth.settings >= 0 && cloth.settings < (int32_t)p_skeleton.cloths.size()) {
            const SkeletonCloth& def = p_skeleton.cloths[cloth.settings];
            const vec3 carry = moved * (1.0f - std::clamp(def.inertia, 0.0f, 1.0f));
            const vec3 gravity = vec3(0.0f, -p_gravity, 0.0f) * step * step;
            const float damping = std::clamp(def.damping, 0.0f, 1.0f);
            const float stretch = std::clamp(def.stretch, 0.0f, 1.0f);
            const float bend = std::clamp(def.bend, 0.0f, 1.0f);
            vec3 lo(FLT_MAX), hi(-FLT_MAX);
            for (uint32_t p = 0; p < n; p++) {
                lo = glm::min(lo, cloth.anchor[p]);
                hi = glm::max(hi, cloth.anchor[p]);
            }
            _gather((lo + hi) * 0.5f, length(hi - lo) * 0.5f + std::max(def.max_distance, 0.0f), def.thickness, colliders, moving, boxes, cloth.contacts);
            cloth.reach.resize(n);
            for (uint32_t p = 0; p < n; p++) cloth.reach[p] = parts[p].max_distance * std::max(def.max_distance, 0.0f);
            for (uint32_t p = 0; p < n; p++) {
                if (parts[p].max_distance <= 0.0f) continue;
                cloth.pos[p] += carry;
                cloth.prev[p] += carry;
            }
            for (uint32_t s = 0; s < steps; s++) {
                for (uint32_t p = 0; p < n; p++) {
                    if (parts[p].max_distance <= 0.0f) {
                        cloth.pos[p] = cloth.prev[p] = cloth.anchor[p];
                        continue;
                    }
                    const vec3 velocity = (cloth.pos[p] - cloth.prev[p]) * (1.0f - damping);
                    cloth.prev[p] = cloth.pos[p];
                    cloth.pos[p] += velocity + gravity;
                }
                vec3* pos = cloth.pos.data();
                const vec3* anchor = cloth.anchor.data();
                const float* reach = cloth.reach.data();
                const LMeshClothLink* links = dyn.links.data() + src.link_first;
                const uint32_t link_count = src.stretch_count + src.bend_count;
                for (uint32_t it = 0; it < CLOTH_ITERATIONS; it++) {
                    for (uint32_t l = 0; l < link_count; l++) {
                        const LMeshClothLink& link = links[l];
                        const float wa = reach[link.a] > 0.0f ? 1.0f : 0.0f;
                        const float wb = reach[link.b] > 0.0f ? 1.0f : 0.0f;
                        if (wa + wb <= 0.0f) continue;
                        vec3& pa = pos[link.a];
                        vec3& pb = pos[link.b];
                        const float dx = pb.x - pa.x, dy = pb.y - pa.y, dz = pb.z - pa.z;
                        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                        if (len < DYNAMICS_EPSILON) continue;
                        const float k = (len - link.rest * scale) / (len * (wa + wb)) * (l < src.stretch_count ? stretch : bend);
                        const float cx = dx * k, cy = dy * k, cz = dz * k;
                        pa.x += cx * wa;
                        pa.y += cy * wa;
                        pa.z += cz * wa;
                        pb.x -= cx * wb;
                        pb.y -= cy * wb;
                        pb.z -= cz * wb;
                    }
                    for (uint32_t p = 0; p < n; p++) {
                        if (reach[p] <= 0.0f) continue;
                        vec3& q = pos[p];
                        const float ox = q.x - anchor[p].x, oy = q.y - anchor[p].y, oz = q.z - anchor[p].z;
                        const float d2 = ox * ox + oy * oy + oz * oz;
                        if (d2 <= reach[p] * reach[p]) continue;
                        const float k = reach[p] / std::sqrt(d2);
                        q.x = anchor[p].x + ox * k;
                        q.y = anchor[p].y + oy * k;
                        q.z = anchor[p].z + oz * k;
                    }
                }
                if (!cloth.contacts.empty()) {
                    for (uint32_t p = 0; p < n; p++) {
                        if (reach[p] > 0.0f) _collide(pos[p], cloth.contacts);
                    }
                }
            }
        } else {
            cloth.pos = cloth.anchor;
            cloth.prev = cloth.anchor;
        }

        std::vector<vec3>& local = cloth.local;
        local.resize(n);
        for (uint32_t p = 0; p < n; p++) {
            const vec3 shown = parts[p].max_distance <= 0.0f ? cloth.anchor[p] : mix(cloth.prev[p], cloth.pos[p], alpha);
            local[p] = vec3(inv_model * vec4(shown, 1.0f));
        }
        for (uint32_t p = 0; p < n; p++) {
            vec3 normal(0.0f);
            for (uint32_t k = cloth.tri_offset[p]; k < cloth.tri_offset[p + 1]; k++) {
                const u16vec4 tri = dyn.triangles[src.triangle_first + cloth.tri_list[k]];
                normal += cross(local[tri.y] - local[tri.x], local[tri.z] - local[tri.x]);
            }
            const vec3 toward = cloth.neighbor[p] >= 0 ? local[cloth.neighbor[p]] - local[p] : vec3(1.0f, 0.0f, 0.0f);
            const mat3 rotation = _frame(normal, toward) * transpose(cloth.rest_frame[p]);
            r_skin[src.joint_first + p] = translate(mat4(1.0f), local[p]) * mat4(rotation) * translate(mat4(1.0f), -parts[p].rest);
        }
    };

    if (cloths.size() == 1) solve(0);
    else job_system().parallel_for((uint32_t)cloths.size(), 1, solve, JobPriority::HIGH, "Cloth");
}

void CharacterDynamics::hold(const mat4& p_model)
{
    if (!initialized) return;
    const mat4 delta = p_model * inverse(frame);
    const auto carry = [&](std::vector<vec3>& r_points) {
        for (vec3& p : r_points) p = vec3(delta * vec4(p, 1.0f));
    };
    for (DynamicsChain& chain : chains) {
        carry(chain.pos);
        carry(chain.prev);
    }
    for (DynamicsCloth& cloth : cloths) {
        carry(cloth.pos);
        carry(cloth.prev);
    }
    finish(p_model);
}

void CharacterDynamics::finish(const mat4& p_model)
{
    origin = vec3(p_model[3]);
    frame = p_model;
    initialized = true;
}

void CharacterDynamics::follow_chains(const mat4& p_model, const std::vector<mat4>& p_global)
{
    for (DynamicsChain& chain : chains) {
        chain.anim.resize(chain.joints.size());
        for (size_t k = 0; k < chain.joints.size(); k++) chain.anim[k] = vec3(p_model * p_global[chain.joints[k]][3]);
        chain.pos = chain.anim;
        chain.prev = chain.anim;
    }
}

void CharacterDynamics::draw(DebugDraw& draw, bool p_chains, bool p_cloth) const
{
    for (const DynamicsChain& chain : chains) {
        if (!p_chains) break;
        for (size_t k = 0; k < chain.pos.size(); k++) {
            draw.sphere(chain.pos[k], 0.01f, DebugDraw::Style{ .color = k == 0 ? DebugDraw::RED : DebugDraw::YELLOW });
            if (chain.parent[k] >= 0) draw.line(chain.pos[chain.parent[k]], chain.pos[k], DebugDraw::Style{ .color = DebugDraw::YELLOW });
        }
    }
    for (const DynamicsCloth& cloth : cloths) {
        if (!p_cloth) break;
        for (size_t p = 0; p < cloth.pos.size(); p++) draw.sphere(cloth.pos[p], 0.006f, DebugDraw::Style{ .color = DebugDraw::ORANGE });
    }
}

}
