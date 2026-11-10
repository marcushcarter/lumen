#include <core/game/player_camera.h>
#include <core/input/input.h>
#include <core/physics/physics_world.h>
#include <cmath>

namespace lumen {

static constexpr uint32_t CAMERA_COLLISION_LAYERS = physics_layer_bit(PhysicsLayer::STATIC) | physics_layer_bit(PhysicsLayer::KINEMATIC);

static float _alpha(float p_dt, float p_tau)
{
    return p_tau <= 0.0f ? 1.0f : 1.0f - std::exp(-p_dt / p_tau);
}

static CameraState _requested(uint32_t p_requests)
{
    static const CameraState PRIORITY[] = { CameraState::AIM, CameraState::BLOCK, CameraState::LOCK_ON, CameraState::MOUNTED, CameraState::SPRINT };
    for (CameraState s : PRIORITY) {
        if (p_requests & (1u << (uint32_t)s)) return s;
    }
    return CameraState::EXPLORE;
}

static float _cast(const PhysicsWorld* p_physics, const vec3& p_from, const vec3& p_to, float p_radius)
{
    const vec3 delta = p_to - p_from;
    const float len = length(delta);
    if (!p_physics || len <= 1e-4f) return len;
    PhysicsHit hit;
    PhysicsQuery query;
    query.layers = CAMERA_COLLISION_LAYERS;
    if (!p_physics->sphere_cast(p_from, p_radius, delta, len, hit, query)) return len;
    return std::max(hit.distance, 0.0f);
}

void PlayerCamera::update(const World& p_world, Entity p_target, const CameraSettings& p_settings, const Input* p_input, const PhysicsWorld* p_physics, float p_dt, Camera& r_camera)
{
    if (!p_world.valid(p_target)) return;
    const mat4 model = p_world.world_matrix(p_target);
    const vec3 target = vec3(model[3]) + vec3(0.0f, p_settings.pivot_height, 0.0f);
    const float scale = std::max(p_settings.distance_scale, 0.01f);

    if (!initialized) {
        const vec3 facing = vec3(model * vec4(0.0f, 0.0f, -1.0f, 0.0f));
        yaw = (facing.x != 0.0f || facing.z != 0.0f) ? degrees(std::atan2(-facing.x, -facing.z)) : 0.0f;
        pitch = p_settings.start_pitch;
        state = CameraState::EXPLORE;
        const CameraStateSettings& explore = p_settings.states[(uint32_t)CameraState::EXPLORE];
        blend_time = explore.enter_time;
        distance = explore.distance;
        offset = explore.offset;
        fov = explore.fov;
        collision_distance = distance * scale;
        pivot = target;
        initialized = true;
    } else if (glm::distance(pivot, target) > SNAP_DISTANCE) {
        pivot = target;
    }

    if (p_input) {
        const vec2 look = p_input->axis(InputAction::LOOK);
        const float invert = p_settings.invert_y ? -1.0f : 1.0f;
        if (p_input->action(InputAction::LOOK).device == InputDevice::GAMEPAD) {
            yaw -= look.x * p_settings.stick_sensitivity * p_dt;
            pitch += look.y * p_settings.stick_sensitivity * p_dt * invert;
        } else {
            yaw -= look.x * p_settings.mouse_sensitivity;
            pitch -= look.y * p_settings.mouse_sensitivity * invert;
        }
    }
    pitch = clamp(pitch, p_settings.pitch_min, p_settings.pitch_max);
    yaw = std::remainder(yaw, 360.0f);

    const CameraState next = _requested(requests);
    requests = 0;
    if (next != state) {
        blend_time = next != CameraState::EXPLORE ? p_settings.states[(uint32_t)next].enter_time : p_settings.states[(uint32_t)state].exit_time;
        state = next;
    }
    const CameraStateSettings& goal = p_settings.states[(uint32_t)state];
    const float a = _alpha(p_dt, blend_time * BLEND_TIME_SCALE);
    distance = mix(distance, goal.distance, a);
    offset = mix(offset, goal.offset, a);
    fov = mix(fov, goal.fov, a);

    const float ah = _alpha(p_dt, p_settings.pivot_lag);
    const float av = _alpha(p_dt, p_settings.pivot_lag_vertical);
    pivot.x = mix(pivot.x, target.x, ah);
    pivot.z = mix(pivot.z, target.z, ah);
    pivot.y = mix(pivot.y, target.y, av);

    const quat rotation = angleAxis(radians(yaw), vec3(0.0f, 1.0f, 0.0f)) * angleAxis(radians(pitch), vec3(1.0f, 0.0f, 0.0f));
    const vec3 forward = rotation * vec3(0.0f, 0.0f, -1.0f);
    const vec3 right = rotation * vec3(1.0f, 0.0f, 0.0f);
    const vec3 up = rotation * vec3(0.0f, 1.0f, 0.0f);

    const float radius = std::max(p_settings.probe_radius, 0.01f);
    const vec3 shoulder_goal = pivot + right * offset.x + up * offset.y;
    const vec3 shoulder_dir = shoulder_goal - pivot;
    const float shoulder_len = length(shoulder_dir);
    const vec3 shoulder = shoulder_len > 1e-4f ? pivot + shoulder_dir / shoulder_len * _cast(p_physics, pivot, shoulder_goal, radius) : pivot;

    const float wanted = distance * scale;
    const float allowed = std::max(_cast(p_physics, shoulder, shoulder - forward * wanted, radius), MIN_COLLISION_DISTANCE);
    if (allowed < collision_distance) collision_distance = allowed;
    else collision_distance = mix(collision_distance, allowed, _alpha(p_dt, p_settings.return_time * BLEND_TIME_SCALE));

    r_camera.rotation = rotation;
    r_camera.position = shoulder - forward * collision_distance;
    r_camera.fov_y = radians(fov);
}

}
