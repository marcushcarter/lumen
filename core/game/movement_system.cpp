#include <core/game/movement_system.h>
#include <core/game/physics_system.h>
#include <core/scene/components.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

static const vec3 MOVE_UP(0.0f, 1.0f, 0.0f);
static constexpr float TELEPORT_DISTANCE = 0.5f;

static vec3 _flat(const vec3& p_v)
{
    const vec3 v(p_v.x, 0.0f, p_v.z);
    const float len = length(v);
    return len > 1e-5f ? v / len : vec3(0.0f);
}

static float _wrap_angle(float p_radians)
{
    p_radians = std::fmod(p_radians + glm::pi<float>(), glm::two_pi<float>());
    if (p_radians < 0.0f) p_radians += glm::two_pi<float>();
    return p_radians - glm::pi<float>();
}

void MovementSystem::start(GameSession& r_session)
{
    (void)r_session;
    _clear();
}

void MovementSystem::stop(GameSession& r_session)
{
    (void)r_session;
    _clear();
}

void MovementSystem::_clear()
{
    if (physics && physics->physics.initialized()) {
        for (auto& [k, body] : bodies) physics->physics.destroy_character(body.id);
    }
    bodies.clear();
}

void MovementSystem::_player_input(const GameSession& p_session, const Scene& p_world, Entity p_entity, MovementComponent& r_move) const
{
    (void)p_world;
    (void)p_entity;
    const vec3 look = p_session.camera.rotation * vec3(0.0f, 0.0f, -1.0f);
    vec3 forward = _flat(look);
    if (forward == vec3(0.0f)) forward = vec3(std::sin(r_move.facing), 0.0f, std::cos(r_move.facing));
    const vec3 right(-forward.z, 0.0f, forward.x);
    const vec2 input = p_session.input.move;
    vec3 want = right * input.x + forward * input.y;
    const float mag = length(want);
    if (mag > 1.0f) want /= mag;
    r_move.want_move = want;
    r_move.want_look = look;
    if (p_session.input.crouch_toggle) r_move.want_crouch = !r_move.want_crouch;
    if (p_session.input.roll && r_move.grounded && !r_move.rolling) r_move.want_roll = true;
    if (p_session.input.jump) r_move.jump_buffer = JUMP_BUFFER;
    if (p_session.input.sprint && mag > 0.01f) r_move.want_crouch = false;
    if (p_session.input.sprint) r_move.want_gait = MoveGait::SPRINT;
    else r_move.want_gait = std::min(mag, 1.0f) < WALK_INPUT ? MoveGait::WALK : MoveGait::RUN;
}

void MovementSystem::update(GameSession& r_session, float p_dt)
{
    if (!r_session.world || !physics || !physics->physics.initialized() || p_dt <= 0.0f) return;
    PhysicsWorld& pw = physics->physics;
    Scene& world = *r_session.world;
    const vec3 focus = physics->focus;
    const float gravity = world.physics.gravity;
    for (auto& [k, body] : bodies) body.seen = false;

    world.view<TransformComponent, MovementComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, TransformComponent& r_transform, MovementComponent& r_move) {
        const vec3 position = r_transform.position;
        if (!r_move.facing_ready) {
            const vec3 forward = r_transform.rotation * vec3(0.0f, 0.0f, 1.0f);
            r_move.facing = std::atan2(forward.x, forward.z);
            r_move.target_facing = r_move.facing;
            r_move.facing_ready = true;
        }
        r_move.jump_buffer = std::max(r_move.jump_buffer - p_dt, 0.0f);
        r_move.jump_lock = std::max(r_move.jump_lock - p_dt, 0.0f);
        if (r_move.player) _player_input(r_session, world, p_entity, r_move);

        const uint64_t k = PhysicsSystem::key(p_entity);
        auto it = bodies.find(k);
        const float range = length(position - focus);
        if (!r_move.player && range > PhysicsSystem::STREAM_UNLOAD) {
            if (it != bodies.end()) {
                pw.destroy_character(it->second.id);
                bodies.erase(it);
            }
            r_move.simulated = false;
            r_move.velocity = vec3(0.0f);
            return;
        }
        if (it == bodies.end()) {
            if (!r_move.player && range > PhysicsSystem::STREAM_LOAD) {
                r_move.simulated = false;
                return;
            }
            Body body;
            body.entity = p_entity;
            body.id = pw.create_character(position, r_move.radius, r_move.height, r_move.max_slope);
            body.radius = r_move.radius;
            body.height = r_move.height;
            body.max_slope = r_move.max_slope;
            if (!body.id.valid()) return;
            it = bodies.emplace(k, body).first;
        }
        Body& body = it->second;
        body.seen = true;
        const float height = r_move.want_crouch ? std::min(r_move.crouch_height, r_move.height) : r_move.height;
        if (body.radius != r_move.radius || body.height != height) {
            if (pw.set_character_shape(body.id, r_move.radius, height)) {
                body.radius = r_move.radius;
                body.height = height;
            }
        }
        r_move.crouching = body.height < r_move.height;
        if (body.max_slope != r_move.max_slope) {
            pw.set_character_max_slope(body.id, r_move.max_slope);
            body.max_slope = r_move.max_slope;
        }

        const CameraComponent* cam = world.try_get<CameraComponent>(p_entity);
        r_move.strafing = r_move.strafe || (r_move.player && cam && cam->type == CameraType::FIRST_PERSON);

        if (!body.landed) {
            PhysicsHit hit;
            if (!pw.raycast(position + MOVE_UP * (r_move.height * 0.5f), -MOVE_UP, GROUND_PROBE, hit)) {
                r_move.simulated = false;
                return;
            }
        }

        const vec3 want(r_move.want_move.x, 0.0f, r_move.want_move.z);
        const float mag = std::min(length(want), 1.0f);
        const bool moving = mag > 0.01f;
        const vec3 look = _flat(r_move.want_look);
        vec3 horizontal(r_move.velocity.x, 0.0f, r_move.velocity.z);
        float speed = length(horizontal);
        if (r_move.strafing && look != vec3(0.0f)) r_move.target_facing = std::atan2(look.x, look.z);
        else if (moving && speed > PIVOT_SPEED && !r_move.pivoting) r_move.target_facing = std::atan2(horizontal.x, horizontal.z);
        else if (moving && !r_move.pivoting) r_move.target_facing = std::atan2(want.x, want.z);

        float gait_speed = r_move.crouching ? r_move.crouch_speed : r_move.gait_speed(r_move.want_gait);
        if (!r_move.crouching && r_move.want_gait == MoveGait::SPRINT && moving && r_move.strafing) {
            const vec3 ahead(std::sin(r_move.target_facing), 0.0f, std::cos(r_move.target_facing));
            const float forward = std::max(dot(normalize(want), ahead), 0.0f);
            gait_speed = r_move.run_speed + (r_move.sprint_speed - r_move.run_speed) * forward;
        }
        const float target_speed = moving ? mag * gait_speed : 0.0f;
        const float grip = r_move.foot_planted ? 1.0f : AIR_GRIP;
        const vec3 desired = moving ? normalize(want) * target_speed : vec3(0.0f);
        vec3 drift = desired - horizontal;
        {
            const float rate = dot(desired, desired) >= dot(horizontal, horizontal) ? r_move.acceleration : r_move.deceleration;
            const float step = rate * p_dt;
            const float drift_len = length(drift);
            if (drift_len > step) drift *= step / drift_len;
        }
        const vec3 floaty = horizontal + drift;
        if (moving && speed > PIVOT_SPEED) {
            const vec3 dir = horizontal / speed;
            const vec3 want_dir = normalize(want);
            const float cos_angle = dot(dir, want_dir);
            if (cos_angle < std::cos(glm::radians(PIVOT_ANGLE))) r_move.pivoting = true;
            else if (cos_angle > std::cos(glm::radians(PIVOT_ANGLE - PIVOT_HYSTERESIS))) r_move.pivoting = false;
            if (r_move.pivoting) {
                speed = std::max(speed - r_move.brake * grip * p_dt, 0.0f);
                horizontal = dir * speed;
            } else {
                const float angle = std::atan2(dir.z * want_dir.x - dir.x * want_dir.z, cos_angle);
                const float max_turn = glm::radians(r_move.redirect_rate) * grip * p_dt;
                const vec3 turned = angleAxis(std::clamp(angle, -max_turn, max_turn), MOVE_UP) * dir;
                const float rate = (target_speed >= speed ? r_move.acceleration : r_move.deceleration) * grip;
                speed = target_speed >= speed ? std::min(speed + rate * p_dt, target_speed) : std::max(speed - rate * p_dt, target_speed);
                horizontal = turned * speed;
            }
            horizontal = mix(floaty, horizontal, std::clamp(r_move.responsiveness, 0.0f, 1.0f));
        } else {
            r_move.pivoting = false;
            horizontal = floaty;
        }
        if (!r_move.grounded) {
            horizontal = vec3(r_move.velocity.x, 0.0f, r_move.velocity.z);
            vec3 steer = desired - horizontal;
            const float step = r_move.acceleration * std::clamp(r_move.air_control, 0.0f, 1.0f) * p_dt;
            const float steer_len = length(steer);
            if (steer_len > step) steer *= step / steer_len;
            if (moving) horizontal += steer;
            r_move.pivoting = false;
        }
        if (r_move.rolling) {
            horizontal = vec3(0.0f);
            r_move.pivoting = false;
        }
        if (r_move.grounded) r_move.air_time = 0.0f;
        else r_move.air_time += p_dt;
        float vertical = r_move.grounded ? 0.0f : r_move.velocity.y - gravity * p_dt;
        if (r_move.jump_buffer > 0.0f && r_move.jump_lock <= 0.0f && !r_move.rolling && (r_move.grounded || r_move.air_time < COYOTE_TIME)) {
            vertical = std::sqrt(2.0f * std::max(gravity, 0.0f) * std::max(r_move.jump_height, 0.0f));
            r_move.grounded = false;
            r_move.jump_buffer = 0.0f;
            r_move.jump_lock = JUMP_LOCK;
            r_move.air_time = COYOTE_TIME;
            r_move.fall_speed = 0.0f;
            r_move.jumped = true;
            r_move.want_crouch = false;
        }
        const bool was_grounded = r_move.grounded;

        const vec3 root = vec3(r_move.root_motion.x, 0.0f, r_move.root_motion.z) / p_dt;
        r_move.root_motion = vec3(0.0f);
        PhysicsCharacterMove move;
        move.velocity = vec3(horizontal.x + root.x, vertical, horizontal.z + root.z);
        move.step_up = r_move.step_up;
        move.stick_down = r_move.grounded ? move.stick_down : 0.0f;
        PhysicsCharacterResult result;
        if (!pw.move_character(body.id, move, p_dt, result)) return;
        r_move.grounded = result.on_ground && !(r_move.jump_lock > 0.0f && result.velocity.y > 0.01f);
        body.landed = body.landed || result.on_ground || result.on_steep_ground;
        r_move.velocity = result.velocity - root;
        if (!r_move.grounded) r_move.fall_speed = std::max(r_move.fall_speed, -r_move.velocity.y);
        if (r_move.grounded && !was_grounded) {
            r_move.land_speed = r_move.fall_speed;
            r_move.fall_speed = 0.0f;
        }
        if (r_move.grounded && r_move.velocity.y < 0.0f) r_move.velocity.y = 0.0f;

        if (length(result.position - position) > TELEPORT_DISTANCE + length(move.velocity) * p_dt) {
            pw.set_character_position(body.id, position);
        } else {
            r_transform.position = result.position;
        }

        const bool turn_in_place = world.has<LocomotionComponent>(p_entity);
        if (!r_move.rolling && (moving || !turn_in_place)) {
            const float delta = _wrap_angle(r_move.target_facing - r_move.facing);
            const float max_turn = glm::radians(r_move.turn_rate) * p_dt;
            r_move.facing = _wrap_angle(r_move.facing + std::clamp(delta, -max_turn, max_turn));
        }
        r_transform.rotation = angleAxis(r_move.facing, MOVE_UP);
        r_move.simulated = true;
    });

    for (auto it = bodies.begin(); it != bodies.end();) {
        if (it->second.seen) {
            ++it;
            continue;
        }
        pw.destroy_character(it->second.id);
        it = bodies.erase(it);
    }
}

}
