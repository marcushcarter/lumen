#include <core/game/camera_system.h>
#include <core/animation/skinning.h>
#include <core/game/physics_system.h>
#include <core/rendering/renderer.h>
#include <core/scene/components.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

static const vec3 CAMERA_UP(0.0f, 1.0f, 0.0f);

static float _interp(float p_current, float p_target, float p_dt, float p_speed)
{
    return p_current + (p_target - p_current) * std::clamp(p_dt * p_speed, 0.0f, 1.0f);
}

static float _wrap(float p_degrees)
{
    p_degrees = std::fmod(p_degrees + 180.0f, 360.0f);
    if (p_degrees < 0.0f) p_degrees += 360.0f;
    return p_degrees - 180.0f;
}

static vec3 _look(float p_yaw, float p_pitch)
{
    const float yaw = glm::radians(p_yaw);
    const float pitch = glm::radians(p_pitch);
    return vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
}

static void _pitch_range(const CameraComponent& p_cam, float& r_min, float& r_max)
{
    const bool fp = p_cam.type == CameraType::FIRST_PERSON;
    r_min = fp ? p_cam.fp_pitch_min : p_cam.pitch_min;
    r_max = fp ? p_cam.fp_pitch_max : p_cam.pitch_max;
}

const PhysicsWorld* CameraSystem::_physics() const
{
    return physics && physics->physics.initialized() ? &physics->physics : nullptr;
}

void CameraSystem::start(GameSession&)
{
    target = ENTITY_NULL;
    ready = false;
    blocked = -1.0f;
    eye_ready = false;
}

void CameraSystem::stop(GameSession& r_session)
{
    (void)r_session;
    target = ENTITY_NULL;
    ready = false;
    blocked = -1.0f;
    eye_ready = false;
}

vec3 CameraSystem::_pivot_target(Scene& p_world) const
{
    const CameraComponent& cam = *p_world.try_get<CameraComponent>(target);
    float height = cam.pivot_height;
    if (const MovementComponent* move = p_world.try_get<MovementComponent>(target); move && move->crouching) height -= move->height - move->crouch_height;
    return p_world.world_transform(target).position + CAMERA_UP * height;
}

vec3 CameraSystem::_eye_target(Scene& p_world) const
{
    const CameraComponent& cam = *p_world.try_get<CameraComponent>(target);
    const mat4 model = p_world.world_matrix(target);
    vec3 head = vec3(model[3]) + CAMERA_UP * cam.pivot_height;
    const PoseComponent* pose = p_world.try_get<PoseComponent>(target);
    if (renderer && pose && !pose->joints.empty()) {
        if (const SkeletonCache::Entry* skeleton = renderer->skeletons.get(pose->skeleton)) {
            const int32_t j = skeleton_find_head(skeleton->skeleton, cam.head_bone);
            if (j != SKELETON_NO_JOINT && j < (int32_t)pose->joints.size()) head = vec3(model * pose->joints[j][3]);
        }
    }
    return head + mat3(model) * cam.eye_offset;
}

void CameraSystem::update(GameSession& r_session, float p_dt)
{
    (void)p_dt;
    if (!r_session.world) return;
    Scene& world = *r_session.world;
    Entity best = ENTITY_NULL;
    bool best_player = false;
    world.view<CameraComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, CameraComponent& r_cam) {
        r_cam.viewing = false;
        const bool player = character_is_player(world, p_entity);
        if (best == ENTITY_NULL || (player && !best_player)) {
            best = p_entity;
            best_player = player;
        }
    });
    const bool keep = world.valid(target) && world.has<CameraComponent>(target) && !world.has<DisabledTag>(target) && (character_is_player(world, target) || !best_player);
    if (!keep && target != best) {
        target = best;
        ready = false;
        blocked = -1.0f;
        eye_ready = false;
    }
    if (target == ENTITY_NULL) return;

    CameraComponent& cam = *world.try_get<CameraComponent>(target);
    cam.viewing = true;
    if (r_session.input.camera_toggle && character_is_player(world, target)) {
        cam.type = cam.type == CameraType::ORBIT ? CameraType::FIRST_PERSON : CameraType::ORBIT;
        eye_ready = false;
    }
    float pitch_min, pitch_max;
    _pitch_range(cam, pitch_min, pitch_max);
    if (!ready) {
        ready = true;
        const MovementComponent* movement = world.try_get<MovementComponent>(target);
        if (movement && movement->facing_ready) {
            yaw = _wrap(glm::degrees(movement->facing));
        } else {
            const vec3 forward = world.world_transform(target).rotation * vec3(0.0f, 0.0f, 1.0f);
            yaw = _wrap(glm::degrees(std::atan2(forward.x, forward.z)));
        }
        pitch = std::clamp(cam.type == CameraType::FIRST_PERSON ? 0.0f : START_PITCH, pitch_min, pitch_max);
        pivot = _pivot_target(world);
        distance = cam.distance;
        shoulder = cam.shoulder;
    }
    const MovementComponent* rolling = world.try_get<MovementComponent>(target);
    if (!rolling || (!rolling->rolling && !rolling->mantling)) {
        yaw = _wrap(yaw - r_session.input.look.x);
        pitch = std::clamp(pitch - r_session.input.look.y, pitch_min, pitch_max);
    }
    r_session.camera.rotation = quatLookAt(_look(yaw, pitch), CAMERA_UP);
}

void CameraSystem::place(GameSession& r_session, float p_dt)
{
    if (!r_session.world || target == ENTITY_NULL || !ready) return;
    Scene& world = *r_session.world;
    if (!world.valid(target)) return;
    const CameraComponent* cam = world.try_get<CameraComponent>(target);
    if (!cam) return;
    if (cam->type == CameraType::FIRST_PERSON) {
        CameraComponent* view = world.try_get<CameraComponent>(target);
        const MovementComponent* move = world.try_get<MovementComponent>(target);
        const bool acting = move && (move->rolling || move->mantling) && cam->actions_third_person;
        third_person = std::clamp(third_person + (acting ? p_dt / ACTION_BLEND_IN : -p_dt / ACTION_BLEND_OUT), 0.0f, 1.0f);
        view->third_person = third_person;
        _place_first_person(r_session, p_dt, !cam->actions_third_person);
        const vec3 body = world.world_transform(target).position;
        if (acting && anchor_weight <= 0.0f) anchor = r_session.camera.position - body;
        anchor_weight = acting ? 1.0f : std::max(anchor_weight - p_dt / ACTION_BLEND_OUT, 0.0f);
        if (third_person <= 0.0f) {
            blocked = -1.0f;
            distance = cam->distance;
            shoulder = cam->shoulder;
            return;
        }
        const float a = anchor_weight * anchor_weight * (3.0f - 2.0f * anchor_weight);
        const vec3 eye_position = mix(r_session.camera.position, body + anchor, a);
        const quat eye_rotation = r_session.camera.rotation;
        _place_orbit(r_session, p_dt);
        const float k = third_person * third_person * third_person * (third_person * (third_person * 6.0f - 15.0f) + 10.0f);
        r_session.camera.position = mix(eye_position, r_session.camera.position, k);
        r_session.camera.rotation = normalize(slerp(eye_rotation, r_session.camera.rotation, k));
        return;
    }
    third_person = 0.0f;
    anchor_weight = 0.0f;
    if (CameraComponent* view = world.try_get<CameraComponent>(target)) view->third_person = 0.0f;
    eye_ready = false;
    _place_orbit(r_session, p_dt);
}

void CameraSystem::_place_orbit(GameSession& r_session, float p_dt)
{
    Scene& world = *r_session.world;
    const CameraComponent* cam = world.try_get<CameraComponent>(target);
    distance = _interp(distance, cam->distance, p_dt, ZOOM);
    shoulder = _interp(shoulder, cam->shoulder, p_dt, ZOOM);
    const vec3 goal = _pivot_target(world);
    pivot.x = _interp(pivot.x, goal.x, p_dt, FOLLOW);
    pivot.z = _interp(pivot.z, goal.z, p_dt, FOLLOW);
    pivot.y = _interp(pivot.y, goal.y, p_dt, FOLLOW_VERTICAL);

    const vec3 look = _look(yaw, pitch);
    const float yaw_rad = glm::radians(yaw);
    vec3 at = pivot - look * distance + vec3(-std::cos(yaw_rad), 0.0f, std::sin(yaw_rad)) * shoulder;
    const vec3 offset = at - pivot;
    const float len = length(offset);
    float reach = len;
    if (const PhysicsWorld* pw = _physics()) {
        PhysicsQuery query;
        auto body = physics->bodies.find(PhysicsSystem::key(target));
        if (body != physics->bodies.end()) query.ignore = body->second.id;
        PhysicsHit hit;
        if (len > 1.0e-3f && pw->sphere_cast(pivot, cam->radius, offset / len, len, hit, query)) reach = std::max(hit.distance - WALL_GAP, MIN_DISTANCE);
    }
    if (reach < len - 1.0e-4f) easing = true;
    if (blocked < 0.0f || reach <= blocked || !easing) {
        blocked = reach;
        release_from = reach;
        release = 0.0f;
    } else {
        release = std::min(release + p_dt / RELEASE_TIME, 1.0f);
        const float k = release * release * (3.0f - 2.0f * release);
        blocked = std::min(release_from + (reach - release_from) * k, reach);
        if (release >= 1.0f) {
            release_from = blocked;
            release = 0.0f;
        }
        if (blocked >= len - 1.0e-3f) easing = false;
    }
    if (len > 1.0e-3f) at = pivot + offset / len * std::min(blocked, len);
    r_session.camera.position = at;
    r_session.camera.rotation = quatLookAt(look, CAMERA_UP);
}

void CameraSystem::_place_first_person(GameSession& r_session, float p_dt, bool p_follow_head)
{
    Scene& world = *r_session.world;
    const vec3 goal = _eye_target(world);
    if (!eye_ready || length(goal - eye) > EYE_SNAP) {
        eye = goal;
        eye_ready = true;
    } else {
        eye += (goal - eye) * std::clamp(p_dt * EYE_FOLLOW, 0.0f, 1.0f);
    }
    pivot = _pivot_target(world);
    r_session.camera.position = eye;
    quat rotation = quatLookAt(_look(yaw, pitch), CAMERA_UP);
    const MovementComponent* move = world.try_get<MovementComponent>(target);
    quat head;
    if (p_follow_head && move && move->roll_camera > 0.0f && _head_rotation(world, head)) {
        if (!roll_ready) {
            roll_offset = inverse(head) * rotation;
            roll_ready = true;
        }
        rotation = normalize(slerp(rotation, normalize(head * roll_offset), move->roll_camera * ROLL_CAMERA));
    } else {
        roll_ready = false;
    }
    r_session.camera.rotation = rotation;
}

bool CameraSystem::_head_rotation(Scene& p_world, quat& r_rotation) const
{
    const CameraComponent* cam = p_world.try_get<CameraComponent>(target);
    const PoseComponent* pose = p_world.try_get<PoseComponent>(target);
    if (!renderer || !cam || !pose || pose->joints.empty()) return false;
    const SkeletonCache::Entry* skeleton = renderer->skeletons.get(pose->skeleton);
    if (!skeleton) return false;
    const int32_t j = skeleton_find_head(skeleton->skeleton, cam->head_bone);
    if (j == SKELETON_NO_JOINT || j >= (int32_t)pose->joints.size()) return false;
    mat3 m = mat3(p_world.world_matrix(target) * pose->joints[j]);
    for (int c = 0; c < 3; ++c) {
        const float len = length(m[c]);
        if (len > 1e-6f) m[c] /= len;
    }
    r_rotation = normalize(quat_cast(m));
    return true;
}

}
