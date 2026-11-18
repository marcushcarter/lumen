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

static void _gather(const vec3& p_center, float p_radius, const std::vector<DynamicsCollider>& p_colliders, const std::vector<uint8_t>& p_skip, std::vector<uint16_t>& r_near)
{
    r_near.clear();
    for (size_t i = 0; i < p_colliders.size(); i++) {
        const DynamicsCollider& c = p_colliders[i];
        if (c.radius <= 0.0f || (c.joint >= 0 && c.joint < (int32_t)p_skip.size() && p_skip[c.joint])) continue;
        const vec3 d = p_center - _closest_on_segment(p_center, c.a, c.b);
        const float reach = p_radius + c.radius;
        if (dot(d, d) <= reach * reach) r_near.push_back((uint16_t)i);
    }
}

static void _push_out(vec3& r_p, float p_radius, const std::vector<DynamicsCollider>& p_colliders, const std::vector<uint16_t>& p_near)
{
    for (const uint16_t i : p_near) {
        const DynamicsCollider& c = p_colliders[i];
        const vec3 closest = _closest_on_segment(r_p, c.a, c.b);
        const vec3 d = r_p - closest;
        const float reach = c.radius + p_radius;
        const float dist2 = dot(d, d);
        if (dist2 >= reach * reach) continue;
        r_p = closest + _safe_normalize(d, vec3(0.0f, 1.0f, 0.0f)) * reach;
    }
}

static mat3 _frame(const vec3& p_normal, const vec3& p_toward)
{
    const vec3 n = _safe_normalize(p_normal, vec3(0.0f, 1.0f, 0.0f));
    vec3 t = p_toward - n * dot(p_toward, n);
    t = _safe_normalize(t, std::abs(n.x) < 0.9f ? normalize(cross(n, vec3(1.0f, 0.0f, 0.0f))) : normalize(cross(n, vec3(0.0f, 1.0f, 0.0f))));
    return mat3(t, cross(n, t), n);
}

static uint32_t _substeps(float p_dt, float& r_time, float& r_alpha)
{
    r_time += std::clamp(p_dt, 0.0f, DYNAMICS_MAX_DT);
    const uint32_t steps = std::min((uint32_t)(r_time / DYNAMICS_STEP), DYNAMICS_MAX_STEPS);
    r_time = std::clamp(r_time - (float)steps * DYNAMICS_STEP, 0.0f, DYNAMICS_STEP);
    r_alpha = r_time / DYNAMICS_STEP;
    return steps;
}

void CharacterDynamics::bind(uint64_t p_key, const LSkeleton& p_skeleton, const LMeshRig& p_rig, const std::vector<int32_t>& p_map, const LLocomotion& p_body)
{
    if (key == p_key) return;
    key = p_key;
    body.bind(p_body, p_skeleton);
    initialized = false;
    colliders.clear();
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
    for (const SkeletonCloth& c : p_skeleton.cloths) mark(c.root);

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
        const int32_t root = src.root_joint < p_map.size() ? p_map[src.root_joint] : SKELETON_NO_JOINT;
        for (uint32_t s = 0; s < (uint32_t)p_skeleton.cloths.size() && cloth.settings < 0; s++) {
            if (p_skeleton.cloths[s].root == root) cloth.settings = (int32_t)s;
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

void CharacterDynamics::update_colliders(const std::vector<mat4>& p_global, const mat4& p_model, float p_model_yaw)
{
    if ((chains.empty() && cloths.empty()) || !body.valid) {
        colliders.clear();
        return;
    }
    vec3 a[RAGDOLL_PART_COUNT], b[RAGDOLL_PART_COUNT];
    float radius[RAGDOLL_PART_COUNT];
    body.capsules(p_global, p_model, p_model_yaw, a, b, radius);
    const float scale = _model_scale(p_model);
    colliders.resize(RAGDOLL_PART_COUNT);
    for (uint32_t i = 0; i < RAGDOLL_PART_COUNT; i++) colliders[i] = DynamicsCollider{ a[i], b[i], radius[i] * scale, body.part_joint(i) };
}

void CharacterDynamics::simulate_chains(const LSkeleton& p_skeleton, const mat4& p_model, float p_dt, float p_gravity, JointTransform* r_local, std::vector<mat4>& r_global)
{
    if (chains.empty()) return;
    const vec3 now = vec3(p_model[3]);
    const bool reset = !initialized || distance(now, origin) > DYNAMICS_TELEPORT;
    const vec3 moved = reset ? vec3(0.0f) : now - origin;
    const float step = DYNAMICS_STEP;
    float alpha = 0.0f;
    const uint32_t steps = _substeps(p_dt, chain_time, alpha);
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
        _gather(chain.anim[0], extent, colliders, moving, chain.nearby);
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
                    _push_out(chain.pos[k], def.radius, colliders, chain.nearby);
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
    const uint32_t steps = _substeps(p_dt, cloth_time, alpha);

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
            _gather((lo + hi) * 0.5f, length(hi - lo) * 0.5f + std::max(def.max_distance, 0.0f) + def.thickness, colliders, moving, cloth.nearby);
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
                for (uint32_t it = 0; it < CLOTH_ITERATIONS; it++) {
                    for (uint32_t l = 0; l < src.stretch_count + src.bend_count; l++) {
                        const LMeshClothLink& link = dyn.links[src.link_first + l];
                        const float wa = parts[link.a].max_distance > 0.0f ? 1.0f : 0.0f;
                        const float wb = parts[link.b].max_distance > 0.0f ? 1.0f : 0.0f;
                        if (wa + wb <= 0.0f) continue;
                        const vec3 d = cloth.pos[link.b] - cloth.pos[link.a];
                        const float len = length(d);
                        if (len < DYNAMICS_EPSILON) continue;
                        const float k = l < src.stretch_count ? stretch : bend;
                        const vec3 corr = d * ((len - link.rest * scale) / (len * (wa + wb)) * k);
                        cloth.pos[link.a] += corr * wa;
                        cloth.pos[link.b] -= corr * wb;
                    }
                    for (uint32_t p = 0; p < n; p++) {
                        if (parts[p].max_distance <= 0.0f) continue;
                        const vec3 off = cloth.pos[p] - cloth.anchor[p];
                        const float reach = parts[p].max_distance * std::max(def.max_distance, 0.0f);
                        if (dot(off, off) > reach * reach) cloth.pos[p] = cloth.anchor[p] + normalize(off) * reach;
                        _push_out(cloth.pos[p], def.thickness, colliders, cloth.nearby);
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

void CharacterDynamics::finish(const mat4& p_model)
{
    origin = vec3(p_model[3]);
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
