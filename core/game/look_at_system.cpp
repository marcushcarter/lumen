#include <core/game/look_at_system.h>
#include <core/animation/skinning.h>
#include <core/rendering/renderer.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace lumen {

static const vec3 LOOK_UP(0.0f, 1.0f, 0.0f);

static uint64_t _key(Entity p_entity)
{
    return ((uint64_t)p_entity.index << 32) | p_entity.generation;
}

static std::string _lower(const std::string& p_text)
{
    std::string out = p_text;
    for (char& c : out) c = (char)std::tolower((unsigned char)c);
    return out;
}

static quat _rotation_of(const mat4& p_m)
{
    mat3 r(p_m);
    for (int c = 0; c < 3; ++c) {
        const float len = length(r[c]);
        if (len > 1e-6f) r[c] /= len;
    }
    return normalize(quat_cast(r));
}

void LookAtSystem::start(GameSession& r_session)
{
    (void)r_session;
    lookers.clear();
}

void LookAtSystem::stop(GameSession& r_session)
{
    (void)r_session;
    lookers.clear();
}

void LookAtSystem::_gather(const Scene& p_world)
{
    _targets.clear();
    p_world.view<AttentionPointComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const AttentionPointComponent& p_point) {
        if (!p_point.enabled || p_point.priority <= 0.0f || p_point.radius <= 0.0f) return;
        vec4 local = vec4(p_point.offset, 1.0f);
        const PoseComponent* pose = p_point.head ? p_world.try_get<PoseComponent>(p_entity) : nullptr;
        const SkeletonCache::Entry* entry = pose ? renderer->skeletons.get(pose->skeleton) : nullptr;
        if (entry) {
            const CameraComponent* cam = p_world.try_get<CameraComponent>(p_entity);
            const int32_t head = skeleton_find_head(entry->skeleton, cam ? cam->head_bone : nullptr);
            if (head != SKELETON_NO_JOINT && head < (int32_t)pose->joints.size()) local += vec4(vec3(pose->joints[head][3]), 0.0f);
        }
        _targets.push_back({ _key(p_entity), vec3(p_world.world_matrix(p_entity) * local), p_point.priority, p_point.radius });
    });
}

void LookAtSystem::_rig(Looker& r_looker, const LSkeleton& p_skeleton, const CameraComponent* p_camera)
{
    int32_t neck = SKELETON_NO_JOINT;
    for (uint32_t i = 0; i < p_skeleton.joint_count(); ++i) {
        const std::string lower = _lower(p_skeleton.names[i]);
        if (lower.find("spine") != std::string::npos) r_looker.chain.push_back((int32_t)i);
        else if (neck == SKELETON_NO_JOINT && lower.find("neck") != std::string::npos) neck = (int32_t)i;
    }
    r_looker.spines = (uint32_t)r_looker.chain.size();
    r_looker.neck = neck != SKELETON_NO_JOINT;
    if (r_looker.neck) r_looker.chain.push_back(neck);
    r_looker.head = skeleton_find_head(p_skeleton, p_camera ? p_camera->head_bone : nullptr);
    if (r_looker.head != SKELETON_NO_JOINT) r_looker.chain.push_back(r_looker.head);
}

void LookAtSystem::_look(const Scene& p_world, Entity p_entity, Looker& r_looker, const LookAtComponent& p_look, const LSkeleton& p_skeleton, PoseComponent& r_pose, float p_dt)
{
    const MovementComponent* move = p_world.try_get<MovementComponent>(p_entity);
    const CameraComponent* cam = p_world.try_get<CameraComponent>(p_entity);
    const bool player = move && move->player;
    const bool busy = move && (move->rolling || move->mantling);
    const bool first_person = cam && cam->type == CameraType::FIRST_PERSON && cam->viewing;
    const float yaw_max = glm::radians(p_look.yaw_max);
    const float pitch_max = glm::radians(p_look.pitch_max);

    const mat4 model = p_world.world_matrix(p_entity);
    const mat3 to_model = mat3(inverse(model));
    auto angles = [&](const vec3& p_dir, float& r_yaw, float& r_pitch) {
        const vec3 d = to_model * p_dir;
        r_yaw = std::atan2(d.x, d.z);
        r_pitch = std::atan2(d.y, length(vec2(d.x, d.z)));
    };

    _globals.resize(p_skeleton.joint_count());
    p_skeleton.globals(r_pose.local.data(), _globals.data());
    const int32_t eye_joint = r_looker.head != SKELETON_NO_JOINT ? r_looker.head : r_looker.chain.back();
    const vec3 eye = vec3(model * _globals[eye_joint][3]);

    const bool attend = !busy && (!player || (p_look.camera_weight < 1.0f && !first_person));
    const uint64_t self = _key(p_entity);
    uint64_t best = NO_FOCUS;
    float best_score = player ? std::max(p_look.camera_weight, MIN_SCORE) : MIN_SCORE;
    float yaw_target = 0.0f;
    float pitch_target = 0.0f;
    if (attend) {
        for (const Target& t : _targets) {
            if (t.key == self) continue;
            const vec3 to = t.position - eye;
            const float d = length(to);
            if (d >= t.radius || d < 1e-3f) continue;
            float yaw = 0.0f;
            float pitch = 0.0f;
            angles(to / d, yaw, pitch);
            if (std::abs(yaw) > yaw_max || std::abs(pitch) > pitch_max) continue;
            float score = t.priority * (1.0f - d / t.radius);
            if (t.key == r_looker.focus) score *= KEEP;
            if (score <= best_score) continue;
            best_score = score;
            best = t.key;
            yaw_target = yaw;
            pitch_target = pitch;
        }
    }
    r_looker.focus = best;

    if (best == NO_FOCUS && player && !busy && length(move->want_look) > 1e-4f) {
        angles(move->want_look, yaw_target, pitch_target);
        yaw_target = std::clamp(yaw_target, -yaw_max, yaw_max);
        pitch_target = std::clamp(pitch_target, -pitch_max, pitch_max);
    }
    const float k = std::clamp(p_dt * p_look.speed, 0.0f, 1.0f);
    r_looker.yaw += (yaw_target - r_looker.yaw) * k;
    r_looker.pitch += (pitch_target - r_looker.pitch) * k;

    const vec3 split = p_look.split;
    for (size_t i = 0; i < r_looker.chain.size(); ++i) {
        const int32_t j = r_looker.chain[i];
        float w = split.z + (r_looker.neck ? 0.0f : split.y);
        if (i < r_looker.spines) w = split.x / (float)r_looker.spines;
        else if (r_looker.neck && i == r_looker.spines) w = split.y;
        if (i > 0) p_skeleton.globals(r_pose.local.data(), _globals.data());
        const int32_t parent = p_skeleton.parents[j];
        const quat parent_rot = parent >= 0 ? _rotation_of(_globals[parent]) : quat(1.0f, 0.0f, 0.0f, 0.0f);
        const quat turn = angleAxis(r_looker.yaw * w, LOOK_UP) * angleAxis(r_looker.pitch * w, vec3(-1.0f, 0.0f, 0.0f));
        r_pose.local[j].rotation = normalize(inverse(parent_rot) * turn * _rotation_of(_globals[j]));
    }
    r_pose.joints.resize(p_skeleton.joint_count());
    p_skeleton.globals(r_pose.local.data(), r_pose.joints.data());
}

void LookAtSystem::update(GameSession& r_session, float p_dt)
{
    if (!r_session.world || !renderer) return;
    Scene& world = *r_session.world;
    _gather(world);
    for (auto& [k, l] : lookers) l.seen = false;

    std::vector<Entity> entities;
    world.view<LookAtComponent, PoseComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const LookAtComponent&, const PoseComponent&) { entities.push_back(p_entity); });

    for (Entity e : entities) {
        if (!world.has<LocomotionComponent>(e)) continue;
        PoseComponent& pose = *world.try_get<PoseComponent>(e);
        const SkeletonCache::Entry* entry = renderer->skeletons.get(pose.skeleton);
        if (!entry || entry->skeleton.joint_count() == 0 || pose.local.size() != entry->skeleton.joint_count()) continue;
        Looker& l = lookers[_key(e)];
        l.seen = true;
        if (l.skeleton != pose.skeleton) {
            l = Looker{};
            l.seen = true;
            l.skeleton = pose.skeleton;
            _rig(l, entry->skeleton, world.try_get<CameraComponent>(e));
        }
        if (l.chain.empty()) continue;
        _look(world, e, l, *world.try_get<LookAtComponent>(e), entry->skeleton, pose, p_dt);
    }

    for (auto it = lookers.begin(); it != lookers.end();) {
        if (it->second.seen) ++it;
        else it = lookers.erase(it);
    }
}

}
