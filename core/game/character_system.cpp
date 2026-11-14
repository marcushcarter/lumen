#include <core/game/character_system.h>
#include <core/animation/anim_library.h>
#include <core/game/physics_system.h>
#include <core/rendering/debug_draw.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

static const vec3 UP = vec3(0.0f, 1.0f, 0.0f);

static float _interp(float p_current, float p_target, float p_dt, float p_speed)
{
    return p_current + (p_target - p_current) * std::clamp(p_dt * p_speed, 0.0f, 1.0f);
}

static vec3 _interp(const vec3& p_current, const vec3& p_target, float p_dt, float p_speed)
{
    return p_current + (p_target - p_current) * std::clamp(p_dt * p_speed, 0.0f, 1.0f);
}

static float _map_clamped(float p_value, float p_in_min, float p_in_max, float p_out_min, float p_out_max)
{
    const float t = std::clamp((p_value - p_in_min) / (p_in_max - p_in_min), 0.0f, 1.0f);
    return p_out_min + (p_out_max - p_out_min) * t;
}

void CharacterSystem::start(GameSession& r_session)
{
    camera_base_fov = camera_fov = r_session.camera.fov_y;
    player = ENTITY_NULL;
    camera_ready = false;
}

void CharacterSystem::stop(GameSession& r_session)
{
    PhysicsWorld* pw = _physics_mut();
    if (r_session.world && pw) {
        r_session.world->view<CharacterStateComponent>([&](Entity, CharacterStateComponent& r_state) { pw->destroy_character(r_state.character.body); });
    }
    player = ENTITY_NULL;
    camera_ready = false;
}

void CharacterSystem::respawn(Character& r_character)
{
    const Guid asset = r_character.asset;
    const vec3 position = r_character.spawn_position;
    const float yaw = r_character.spawn_yaw;
    const PhysicsCharacterId body = r_character.body;
    r_character = Character{};
    r_character.asset = asset;
    r_character.spawn_position = position;
    r_character.spawn_yaw = yaw;
    r_character.position = position + UP * SPAWN_LIFT;
    r_character.body = body;
    r_character.body_stance = Stance::STANDING;
    r_character.teleport = true;
    r_character.yaw = r_character.target_yaw = r_character.in_air_yaw = r_character.aim_yaw = yaw;
}

const PhysicsWorld* CharacterSystem::_physics() const
{
    return physics && physics->physics.initialized() ? &physics->physics : nullptr;
}

float CharacterSystem::_height(const Character& p_character, const LocomotionComponent& p_body)
{
    return p_character.stance == Stance::CROUCHING ? p_body.crouch_height : p_body.height;
}

bool CharacterSystem::_cast(float p_height, float p_radius, const vec3& p_position, const vec3& p_direction, float p_distance, PhysicsHit& r_hit) const
{
    const PhysicsWorld* pw = _physics();
    if (!pw) return false;
    const float half = std::max(p_height * 0.5f - p_radius, 0.01f);
    return pw->capsule_cast(p_position + UP * (p_height * 0.5f), quat(1.0f, 0.0f, 0.0f, 0.0f), half, p_radius, p_direction, p_distance, r_hit);
}

bool CharacterSystem::_walkable(const PhysicsHit& p_hit, const vec3& p_position) const
{
    if (p_hit.normal.y >= WALKABLE_COS) return true;
    if (p_hit.normal.y <= 0.0f) return false;
    const PhysicsWorld* pw = _physics();
    if (!pw) return false;
    vec3 inward(p_position.x - p_hit.position.x, 0.0f, p_position.z - p_hit.position.z);
    const float len = length(inward);
    inward = len > 1.0e-4f ? inward / len * FLOOR_PROBE_INSET : vec3(0.0f);
    PhysicsHit floor;
    if (!pw->raycast(p_hit.position + inward + UP * FLOOR_PROBE_HEIGHT, -UP, FLOOR_PROBE_HEIGHT * 2.0f, floor)) return false;
    return floor.normal.y >= WALKABLE_COS;
}

PhysicsWorld* CharacterSystem::_physics_mut() const
{
    return physics && physics->physics.initialized() ? &physics->physics : nullptr;
}

void CharacterSystem::_sync_shape(Character& r_character, const LocomotionComponent& p_body) const
{
    PhysicsWorld* pw = _physics_mut();
    if (!pw || !r_character.body.valid() || r_character.body_stance == r_character.stance) return;
    const float height = _height(r_character, p_body);
    if (pw->set_character_shape(r_character.body, std::min(p_body.radius, height * 0.5f - 0.01f), height)) r_character.body_stance = r_character.stance;
    else r_character.stance = r_character.body_stance;
}

void CharacterSystem::_move(Character& r_character, const LocomotionComponent& p_body, float p_dt) const
{
    PhysicsWorld* pw = _physics_mut();
    if (!pw) {
        r_character.position += r_character.velocity * p_dt;
        return;
    }
    if (!r_character.body.valid()) {
        const float height = _height(r_character, p_body);
        r_character.body = pw->create_character(r_character.position, std::min(p_body.radius, height * 0.5f - 0.01f), height, MAX_SLOPE_DEGREES);
        r_character.body_stance = r_character.stance;
        r_character.teleport = false;
    }
    if (r_character.teleport) {
        pw->set_character_position(r_character.body, r_character.position);
        r_character.teleport = false;
    }

    const bool was_grounded = r_character.grounded;
    const float impact = -r_character.velocity.y;
    const float old_y = r_character.position.y;
    PhysicsCharacterMove move;
    move.velocity = r_character.velocity;
    move.step_up = was_grounded ? p_body.max_step_height : 0.0f;
    move.stick_down = was_grounded && r_character.velocity.y <= 0.0f ? std::max(p_body.max_step_height, STICK_DOWN) : 0.0f;
    PhysicsCharacterResult result;
    if (!pw->move_character(r_character.body, move, p_dt, result)) return;

    r_character.position = result.position;
    r_character.velocity.x = result.velocity.x;
    r_character.velocity.z = result.velocity.z;
    r_character.grounded = result.on_ground && r_character.velocity.y <= 0.0f;
    r_character.velocity.y = r_character.grounded ? 0.0f : result.velocity.y;

    if (was_grounded && r_character.grounded) {
        const float dy = r_character.position.y - old_y;
        const float slope = length(vec3(r_character.velocity.x, 0.0f, r_character.velocity.z)) * p_dt * STEP_SLOPE_ALLOWANCE + STEP_VISUAL_MIN;
        if (std::fabs(dy) > slope) r_character.step_offset -= dy - std::copysign(slope, dy);
    }
    if (!was_grounded && r_character.grounded) r_character.land_speed = impact;
}

void CharacterSystem::_smooth_rotation(Character& r_character, float p_target, float p_target_speed, float p_actor_speed, float p_dt)
{
    const float delta = character_wrap(p_target - r_character.target_yaw);
    const float step = p_target_speed > 0.0f ? p_target_speed * p_dt : std::fabs(delta);
    r_character.target_yaw = character_wrap(r_character.target_yaw + std::clamp(delta, -step, step));
    const float actor_delta = character_wrap(r_character.target_yaw - r_character.yaw);
    const float alpha = p_actor_speed > 0.0f ? std::clamp(p_dt * p_actor_speed, 0.0f, 1.0f) : 1.0f;
    r_character.yaw = character_wrap(r_character.yaw + actor_delta * alpha);
}

void CharacterSystem::_rotate(Character& r_character, const CharacterStance& p_stance, float p_dt)
{
    Character& c = r_character;
    if (c.movement == MovementState::IN_AIR) {
        if (c.rotation_mode == RotationMode::AIMING) {
            _smooth_rotation(c, c.aim_yaw, 0.0f, 15.0f, p_dt);
            c.in_air_yaw = c.yaw;
        } else {
            _smooth_rotation(c, c.in_air_yaw, 0.0f, 5.0f, p_dt);
        }
        return;
    }

    if ((c.moving && c.has_input) || c.speed > MOVING_SPEED) {
        const float rate = p_stance.rotation_rate * _map_clamped(c.aim_yaw_rate, 0.0f, 300.0f, 1.0f, 3.0f) * (c.pivot_boost > 0.0f ? PIVOT_ROTATION_BOOST : 1.0f);
        switch (c.rotation_mode) {
            case RotationMode::VELOCITY_DIRECTION: _smooth_rotation(c, c.velocity_yaw, 800.0f, rate, p_dt); break;
            case RotationMode::LOOKING_DIRECTION: _smooth_rotation(c, c.gait == Gait::SPRINTING ? c.velocity_yaw : c.aim_yaw, 500.0f, rate, p_dt); break;
            case RotationMode::AIMING: _smooth_rotation(c, c.aim_yaw, 1000.0f, 20.0f, p_dt); break;
        }
        return;
    }

    if (c.rotation_mode == RotationMode::AIMING) {
        c.yaw = character_wrap(c.yaw + c.turn_delta);
        c.target_yaw = c.yaw;
        const float delta = character_wrap(c.aim_yaw - c.yaw);
        if (std::fabs(delta) > AIM_LIMIT) _smooth_rotation(c, c.aim_yaw - std::copysign(AIM_LIMIT, delta), 0.0f, 20.0f, p_dt);
        return;
    }
    c.yaw = character_wrap(c.yaw + c.turn_delta);
    c.target_yaw = c.yaw;
}

void CharacterSystem::_simulate(Character& r_character, const LLocomotion& p_asset, const LocomotionComponent& p_body, const GameInput& p_input, float p_aim_yaw, float p_aim_pitch, float p_dt) const
{
    Character& c = r_character;
    c.jumped = false;
    c.landed = false;

    const float previous_aim = c.aim_yaw;
    c.aim_yaw = p_aim_yaw;
    c.aim_pitch = p_aim_pitch;
    c.aim_yaw_rate = std::fabs(character_wrap(c.aim_yaw - previous_aim)) / p_dt;

    if (p_input.crouch_toggle && c.grounded) c.stance = c.stance == Stance::STANDING ? Stance::CROUCHING : Stance::STANDING;
    if (p_input.walk_toggle) c.desired_gait = c.desired_gait == Gait::WALKING ? Gait::RUNNING : Gait::WALKING;
    if (p_input.rotation_toggle) c.desired_rotation_mode = c.desired_rotation_mode == RotationMode::VELOCITY_DIRECTION ? RotationMode::LOOKING_DIRECTION : RotationMode::VELOCITY_DIRECTION;
    c.rotation_mode = p_input.aim ? RotationMode::AIMING : c.desired_rotation_mode;

    const vec3 wish = character_forward(p_aim_yaw) * p_input.move.y + character_right(p_aim_yaw) * p_input.move.x;
    c.input_amount = std::min(length(p_input.move), 1.0f);
    const bool had_input = c.has_input;
    c.has_input = c.input_amount > 0.001f && length(wish) > 1.0e-4f;
    c.stop = had_input && !c.has_input && c.grounded && c.stance == Stance::STANDING && c.speed >= STOP_MIN_SPEED;
    if (c.has_input) c.input_yaw = character_yaw_of(wish);

    const CharacterStance& stance = c.stance == Stance::CROUCHING ? p_asset.crouching : p_asset.standing;
    const bool aiming = c.rotation_mode == RotationMode::AIMING;
    const Gait want = p_input.sprint ? Gait::SPRINTING : c.desired_gait;
    const bool can_sprint = c.has_input && !aiming && c.input_amount > SPRINT_INPUT && (c.rotation_mode == RotationMode::VELOCITY_DIRECTION || std::fabs(character_wrap(c.input_yaw - c.aim_yaw)) < SPRINT_ANGLE);
    Gait allowed = want;
    if (want == Gait::SPRINTING && (c.stance != Stance::STANDING || aiming || !can_sprint)) allowed = Gait::RUNNING;
    const float max_speed = allowed == Gait::WALKING ? stance.walk_speed : (allowed == Gait::RUNNING ? stance.run_speed : stance.sprint_speed);

    const vec3 previous_velocity = c.velocity;
    const vec3 flat(c.velocity.x, 0.0f, c.velocity.z);
    c.pivot = false;
    c.pivot_cooldown = std::max(c.pivot_cooldown - p_dt, 0.0f);
    c.pivot_boost = std::max(c.pivot_boost - p_dt, 0.0f);
    if (c.grounded && c.has_input && c.pivot_cooldown <= 0.0f) {
        const float flat_speed = length(flat);
        const float reversal = std::fabs(character_wrap(c.input_yaw - c.velocity_yaw));
        if (flat_speed >= PIVOT_MIN_SPEED && flat_speed < PIVOT_MAX_SPEED && reversal > PIVOT_ANGLE) {
            c.pivot = true;
            c.pivot_boost = PIVOT_DURATION;
            c.pivot_cooldown = PIVOT_COOLDOWN;
        }
    }
    const vec3 target = c.has_input ? normalize(wish) * c.input_amount * max_speed : vec3(0.0f);
    const float control = c.grounded ? 1.0f : p_asset.air_control;
    float braking = stance.braking * (c.land_brake_time > 0.0f ? c.land_brake_scale : 1.0f);
    if (!c.has_input && c.anim.stop_time >= 0.0f && c.anim.stop_settle > 0.0f) braking = std::max(c.anim.stop_speed / c.anim.stop_settle, 0.5f);
    const float rate = (length(target) > length(flat) ? stance.acceleration : braking) * control;
    c.land_brake_time = std::max(c.land_brake_time - p_dt, 0.0f);
    const vec3 delta = target - flat;
    const float step = rate * p_dt;
    const vec3 next = length(delta) <= step ? target : flat + normalize(delta) * step;
    c.velocity.x = next.x;
    c.velocity.z = next.z;

    if (p_input.jump && c.grounded) {
        if (c.stance == Stance::CROUCHING) {
            c.stance = Stance::STANDING;
        } else {
            c.velocity.y = p_asset.jump_speed;
            c.grounded = false;
            c.jumped = true;
            c.jump_flight = true;
            c.in_air_yaw = c.speed > 1.0f ? c.velocity_yaw : c.yaw;
        }
    }
    if (!c.grounded) c.velocity.y -= p_asset.gravity * p_dt;
    if (c.grounded && !c.jumped) c.in_air_yaw = c.yaw;

    _sync_shape(c, p_body);
    _move(c, p_body, p_dt);
    if (c.grounded) {
        c.ungrounded_time = 0.0f;
        c.jump_flight = false;
    } else {
        c.ungrounded_time += p_dt;
    }
    const MovementState previous_movement = c.movement;
    c.movement = !c.grounded && (c.jump_flight || c.ungrounded_time >= AIR_GRACE) ? MovementState::IN_AIR : MovementState::GROUNDED;
    c.landed = previous_movement == MovementState::IN_AIR && c.movement == MovementState::GROUNDED;
    if (previous_movement == MovementState::GROUNDED && c.movement == MovementState::IN_AIR && !c.jump_flight && c.stance == Stance::CROUCHING) c.stance = Stance::STANDING;
    if (c.landed) {
        c.land_brake_time = LAND_BRAKE_TIME;
        c.land_brake_scale = c.has_input ? LAND_BRAKE_INPUT : LAND_BRAKE_IDLE;
    }
    c.ground_gap = -1.0f;
    if (!c.grounded) {
        PhysicsHit hit;
        const float height = _height(c, p_body);
        if (_cast(height, std::min(p_body.radius, height * 0.5f - 0.01f), c.position, -UP, GROUND_PROBE, hit)) c.ground_gap = hit.distance;
    }
    if (c.landed && c.land_speed >= CharacterAnim::LAND_MIN_SPEED) c.ik.pelvis_velocity -= std::min(c.land_speed * LAND_DIP_SCALE, LAND_DIP_MAX);

    c.acceleration = (c.velocity - previous_velocity) / p_dt;
    {
        const vec3 acc(c.acceleration.x, 0.0f, c.acceleration.z);
        const vec3 vel(c.velocity.x, 0.0f, c.velocity.z);
        vec3 lean(0.0f);
        if (c.movement == MovementState::IN_AIR) {
            const float fall = std::clamp(-c.velocity.y / AIR_LEAN_FALL_SPEED, -1.0f, 1.0f);
            lean = vel / AIR_LEAN_SPEED * fall;
        } else {
            const float limit = dot(acc, vel) > 0.0f ? stance.acceleration : stance.braking;
            const float len = length(acc);
            if (limit > 0.0f && len > 1.0e-4f) lean = acc / len * std::min(len, limit) / limit;
        }
        c.lean_fb = _interp(c.lean_fb, std::clamp(dot(lean, character_forward(c.yaw)), -1.0f, 1.0f), p_dt, LEAN_INTERP);
        c.lean_lr = _interp(c.lean_lr, std::clamp(dot(lean, character_right(c.yaw)), -1.0f, 1.0f), p_dt, LEAN_INTERP);
    }
    {
        const float relative = character_wrap(c.aim_yaw - c.yaw);
        const float weight = 1.0f - std::clamp((std::fabs(relative) - LOOK_FADE_START) / LOOK_FADE_RANGE, 0.0f, 1.0f);
        c.look_yaw = _interp(c.look_yaw, std::clamp(relative, -p_asset.look_yaw_limit, p_asset.look_yaw_limit), p_dt, LOOK_INTERP);
        c.look_pitch = _interp(c.look_pitch, std::clamp(c.aim_pitch, -p_asset.look_pitch_limit, p_asset.look_pitch_limit), p_dt, LOOK_INTERP);
        c.look_weight = _interp(c.look_weight, weight, p_dt, LOOK_INTERP);
    }
    c.speed = length(vec3(c.velocity.x, 0.0f, c.velocity.z));
    c.moving = c.speed > 0.01f;
    if (c.moving) c.velocity_yaw = character_yaw_of(vec3(c.velocity.x, 0.0f, c.velocity.z));
    if (c.speed > stance.run_speed + GAIT_MARGIN) c.gait = allowed == Gait::SPRINTING ? Gait::SPRINTING : Gait::RUNNING;
    else if (c.speed >= stance.walk_speed + GAIT_MARGIN) c.gait = Gait::RUNNING;
    else c.gait = Gait::WALKING;

    _sync_shape(c, p_body);
    _rotate(c, stance, p_dt);
    c.turn_delta = 0.0f;
    c.step_offset = _interp(c.step_offset, 0.0f, p_dt, STEP_SMOOTH);
    if (c.position.y < c.spawn_position.y - RESPAWN_DROP) respawn(c);
}

static void _spring(float& r_value, float& r_velocity, float p_target, float p_omega, float p_dt)
{
    const float f = 1.0f + 2.0f * p_dt * p_omega;
    const float oo = p_omega * p_omega;
    const float hoo = p_dt * oo;
    const float hhoo = p_dt * hoo;
    const float inv = 1.0f / (f + hhoo);
    const float x = f * r_value + p_dt * r_velocity + hhoo * p_target;
    const float v = r_velocity + hoo * (p_target - r_value);
    r_value = x * inv;
    r_velocity = v * inv;
}

bool CharacterSystem::_ground_at(const vec3& p_position, float p_top, PhysicsHit& r_hit) const
{
    const PhysicsWorld* pw = _physics();
    if (!pw) return false;
    const vec3 origin(p_position.x, p_top, p_position.z);
    const vec3 end = origin - UP * (FootIK::TRACE_ABOVE + FootIK::TRACE_BELOW);
    bool found = pw->sphere_cast(origin, FootIK::TRACE_RADIUS, -UP, FootIK::TRACE_ABOVE + FootIK::TRACE_BELOW, r_hit);
    if (found && r_hit.normal.y < WALKABLE_COS) {
        PhysicsHit ray;
        found = pw->raycast(origin, -UP, FootIK::TRACE_ABOVE + FootIK::TRACE_BELOW + FootIK::TRACE_RADIUS, ray) && ray.normal.y >= WALKABLE_COS;
        if (found) r_hit = ray;
    }
    DebugDraw& draw = debug_draw();
    draw.line(origin, found ? r_hit.position : end, DebugDraw::Style{ .color = found ? DebugDraw::YELLOW : DebugDraw::RED });
    if (found) draw.line(r_hit.position, r_hit.position + r_hit.normal * 0.1f, DebugDraw::Style{ .color = DebugDraw::CYAN });
    return found;
}

void CharacterSystem::_update_foot_ik(Character& r_character, const LLocomotion& p_asset, float p_model_yaw, float p_dt) const
{
    FootIK& ik = r_character.ik;
    const bool active = p_asset.foot_ik && ik.valid && r_character.grounded && _physics();
    ik.alpha = _interp(ik.alpha, active ? 1.0f : 0.0f, p_dt, FootIK::ALPHA_INTERP);
    const quat rotation = angleAxis(glm::radians(r_character.yaw + p_model_yaw), UP);
    const vec3 root = r_character.position + UP * r_character.step_offset;
    const vec3 forward = character_forward(r_character.yaw);

    r_character.dynamic_foot = -1;
    r_character.dynamic_cooldown = std::max(r_character.dynamic_cooldown - p_dt, 0.0f);
    const CharacterAnim& an = r_character.anim;
    const bool settled = active && !r_character.has_input && r_character.speed < 0.01f && an.turn_time < 0.0f && an.rotate_weight <= BLEND_MIN_WEIGHT && an.transition_time < 0.0f && an.dynamic_time < 0.0f && an.stop_time < 0.0f;
    if (settled && !r_character.was_settled) {
        for (int i = 0; i < 2; i++) ik.locked[i] = false;
        r_character.still_time = 0.0f;
    }
    r_character.was_settled = settled;
    r_character.still_time = settled ? r_character.still_time + p_dt : 0.0f;
    const bool still = settled && r_character.still_time >= DYNAMIC_SETTLE && ik.planted[0] && ik.planted[1];

    for (int i = 0; i < 2; i++) {
        const float height = ik.model[i].y - ik.rest_height[i];
        const float vertical = ik.started ? (ik.model[i].y - ik.previous_height[i]) / p_dt : 0.0f;
        ik.previous_height[i] = ik.model[i].y;
        if (ik.planted[i]) ik.planted[i] = height < FootIK::UNPLANT_HEIGHT && std::fabs(vertical) < FootIK::UNPLANT_VERTICAL_SPEED;
        else ik.planted[i] = height < FootIK::PLANT_HEIGHT && std::fabs(vertical) < FootIK::PLANT_VERTICAL_SPEED;

        const vec3 foot = root + rotation * ik.model[i];
        float target = 0.0f;
        quat tilt(1.0f, 0.0f, 0.0f, 0.0f);
        PhysicsHit heel;
        if (active && _ground_at(foot, root.y + FootIK::TRACE_ABOVE, heel)) {
            target = std::clamp(heel.position.y - root.y, -FootIK::MAX_DROP, FootIK::MAX_RAISE);
            const vec3 side = normalize(vec3(heel.normal.x, 0.0f, heel.normal.z) - forward * dot(vec3(heel.normal.x, 0.0f, heel.normal.z), forward) + vec3(0.0f, heel.normal.y, 0.0f));
            tilt = normalize(quat(1.0f + dot(UP, side), cross(UP, side).x, cross(UP, side).y, cross(UP, side).z));
            PhysicsHit ball;
            if (_ground_at(foot + forward * FootIK::BALL_DISTANCE, root.y + FootIK::TRACE_ABOVE, ball)) {
                const float rise = ball.position.y - heel.position.y;
                if (std::fabs(rise) < FootIK::MAX_PITCH_STEP) {
                    const float pitch = std::clamp(glm::degrees(std::atan2(rise, FootIK::BALL_DISTANCE)), -FootIK::MAX_PITCH, FootIK::MAX_PITCH);
                    tilt = angleAxis(glm::radians(pitch), character_right(r_character.yaw)) * tilt;
                }
            }
            if (!ik.planted[i] && target < 0.0f) target *= FootIK::SWING_DROP_WEIGHT;
        }
        if (!ik.started) ik.height_offset[i] = target;
        _spring(ik.height_offset[i], ik.height_velocity[i], target, FootIK::FOOT_SPRING, p_dt);
        ik.world_tilt[i] = slerp(ik.world_tilt[i], tilt, std::clamp(p_dt * FootIK::TILT_INTERP, 0.0f, 1.0f));

        const vec3 placed = foot + UP * ik.height_offset[i];
        if (active && ik.planted[i] && !ik.locked[i]) {
            ik.locked[i] = true;
            ik.lock_world[i] = placed;
        }
        if (ik.locked[i]) {
            const vec3 drift(ik.lock_world[i].x - placed.x, 0.0f, ik.lock_world[i].z - placed.z);
            if (still && r_character.dynamic_foot < 0 && r_character.dynamic_cooldown <= 0.0f && length(drift) > DYNAMIC_THRESHOLD) {
                r_character.dynamic_foot = i;
                r_character.dynamic_cooldown = DYNAMIC_RETRIGGER;
                ik.locked[i] = false;
            } else if (!active || !ik.planted[i] || length(drift) > FootIK::MAX_LOCK_DRIFT) {
                ik.locked[i] = false;
            }
        }
        ik.lock_alpha[i] = _interp(ik.lock_alpha[i], ik.locked[i] ? 1.0f : 0.0f, p_dt, ik.locked[i] ? FootIK::LOCK_IN : FootIK::LOCK_OUT);
        const vec3 lock(ik.lock_world[i].x - placed.x, 0.0f, ik.lock_world[i].z - placed.z);
        ik.world_offset[i] = UP * ik.height_offset[i] + lock * ik.lock_alpha[i];
        if (active) {
            DebugDraw& draw = debug_draw();
            draw.point(placed + lock * ik.lock_alpha[i], 6.0f, DebugDraw::Style{ .color = ik.planted[i] ? DebugDraw::GREEN : DebugDraw::BLUE });
            if (ik.locked[i]) {
                draw.sphere(ik.lock_world[i], 0.03f, DebugDraw::Style{ .color = DebugDraw::GREEN });
                draw.line(ik.lock_world[i], placed, DebugDraw::Style{ .color = DebugDraw::ORANGE });
            }
        }
    }

    const float pelvis = active ? std::max(std::min(ik.height_offset[0], ik.height_offset[1]), -FootIK::MAX_PELVIS_DROP) : 0.0f;
    _spring(ik.pelvis_offset, ik.pelvis_velocity, std::min(pelvis, 0.0f), FootIK::PELVIS_SPRING, p_dt);
    if (active && std::fabs(ik.pelvis_offset) > 0.005f) {
        const vec3 hip = root + UP * 0.9f;
        debug_draw().arrow(hip, hip + UP * ik.pelvis_offset, DebugDraw::Style{ .color = DebugDraw::ORANGE });
    }
    ik.started = true;

    const quat inv = inverse(rotation);
    for (int i = 0; i < 2; i++) {
        ik.offset[i] = inv * ik.world_offset[i];
        ik.tilt[i] = normalize(inv * ik.world_tilt[i] * rotation);
    }
}

void CharacterSystem::_update_camera(GameSession& r_session, const Character& p_character, const LocomotionComponent& p_body, float p_dt)
{
    const bool aiming = p_character.rotation_mode == RotationMode::AIMING;
    camera_distance = _interp(camera_distance, aiming ? CAMERA_AIM_DISTANCE : CAMERA_DISTANCE, p_dt, CAMERA_ZOOM);
    camera_shoulder = _interp(camera_shoulder, aiming ? CAMERA_AIM_SHOULDER : CAMERA_SHOULDER, p_dt, CAMERA_ZOOM);
    const vec3 pivot_target = p_character.position + UP * (p_character.step_offset + _height(p_character, p_body) * CAMERA_PIVOT_HEIGHT);
    const vec3 horizontal = _interp(vec3(camera_pivot.x, 0.0f, camera_pivot.z), vec3(pivot_target.x, 0.0f, pivot_target.z), p_dt, CAMERA_FOLLOW);
    camera_pivot = vec3(horizontal.x, _interp(camera_pivot.y, pivot_target.y, p_dt, CAMERA_FOLLOW_VERTICAL), horizontal.z);
    camera_fov = _interp(camera_fov, camera_base_fov + (p_character.gait == Gait::SPRINTING ? glm::radians(CAMERA_SPRINT_FOV) : 0.0f), p_dt, CAMERA_FOV_INTERP);
    r_session.camera.fov_y = camera_fov;

    const float yaw = glm::radians(camera_yaw);
    const float pitch = glm::radians(camera_pitch);
    const vec3 look(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
    vec3 eye = camera_pivot - look * camera_distance + character_right(camera_yaw) * camera_shoulder;

    if (const PhysicsWorld* pw = _physics()) {
        const vec3 offset = eye - camera_pivot;
        const float len = length(offset);
        PhysicsHit hit;
        if (len > 1.0e-3f && pw->sphere_cast(camera_pivot, CAMERA_RADIUS, offset / len, len, hit)) eye = camera_pivot + offset / len * std::max(hit.distance - 0.05f, 0.1f);
    }
    r_session.camera.position = eye;
    r_session.camera.rotation = quatLookAt(look, UP);
}

void CharacterSystem::update(GameSession& r_session, float p_dt)
{
    if (!r_session.world || p_dt <= 0.0f || !anims) return;
    World& world = *r_session.world;

    _entities.clear();
    world.view<LocomotionComponent, TransformComponent>([&](Entity p_entity, const LocomotionComponent&, const TransformComponent&) { _entities.push_back(p_entity); });
    for (Entity e : _entities) {
        if (world.has<CharacterStateComponent>(e)) continue;
        const LocomotionComponent& body = *world.try_get<LocomotionComponent>(e);
        const TransformComponent t = world.world_transform(e);
        CharacterStateComponent state;
        state.character.asset = body.locomotion;
        state.character.spawn_position = t.position;
        state.character.spawn_yaw = character_wrap(character_yaw_of(t.rotation * vec3(0.0f, 0.0f, 1.0f)) - body.model_yaw);
        respawn(state.character);
        world.add<CharacterStateComponent>(e, std::move(state));
    }

    if (!world.valid(player) || !world.has<PlayerTag>(player) || !world.has<CharacterStateComponent>(player)) {
        player = ENTITY_NULL;
        camera_ready = false;
        for (Entity e : _entities) {
            if (!world.has<PlayerTag>(e)) continue;
            player = e;
            break;
        }
    }

    const GameInput idle;
    for (Entity e : _entities) {
        CharacterStateComponent* state = world.try_get<CharacterStateComponent>(e);
        const LocomotionComponent* body = world.try_get<LocomotionComponent>(e);
        if (!state || !body) continue;
        Character& c = state->character;
        c.asset = body->locomotion;
        c.model_yaw = body->model_yaw;
        const LLocomotion* asset = anims->locomotion(c.asset);
        if (!asset) continue;

        const bool is_player = e == player;
        if (is_player && !camera_ready) {
            camera_ready = true;
            camera_yaw = c.yaw;
            camera_pitch = CAMERA_START_PITCH;
            camera_pivot = c.position;
            camera_distance = CAMERA_DISTANCE;
            camera_shoulder = CAMERA_SHOULDER;
        }
        if (is_player) {
            camera_yaw = character_wrap(camera_yaw - r_session.input.look.x);
            camera_pitch = std::clamp(camera_pitch - r_session.input.look.y, CAMERA_PITCH_MIN, CAMERA_PITCH_MAX);
            _simulate(c, *asset, *body, r_session.input, camera_yaw, camera_pitch, p_dt);
        } else {
            _simulate(c, *asset, *body, idle, c.yaw, 0.0f, p_dt);
        }

        const TransformComponent current = world.world_transform(e);
        const mat4 m = translate(mat4(1.0f), c.position + UP * c.step_offset) * mat4_cast(angleAxis(glm::radians(c.yaw + body->model_yaw), UP)) * scale(mat4(1.0f), current.scale);
        world.set_world_matrix(e, m);
        {
            DebugDraw& draw = debug_draw();
            const vec3 base = c.position + UP * 0.05f;
            draw.arrow(base, base + character_forward(c.yaw) * 0.6f, DebugDraw::Style{ .color = DebugDraw::BLUE });
            const vec3 flat(c.velocity.x, 0.0f, c.velocity.z);
            if (length(flat) > 0.05f) draw.arrow(base, base + flat * 0.25f, DebugDraw::Style{ .color = DebugDraw::GREEN });
            if (c.has_input) draw.arrow(base, base + character_forward(c.input_yaw) * c.input_amount, DebugDraw::Style{ .color = DebugDraw::YELLOW });
        }
        _update_foot_ik(c, *asset, body->model_yaw, p_dt);
        if (is_player) _update_camera(r_session, c, *body, p_dt);
    }
}

}
