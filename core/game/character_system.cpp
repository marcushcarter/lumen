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
    (void)r_session;
    player = ENTITY_NULL;
}

void CharacterSystem::stop(GameSession& r_session)
{
    PhysicsWorld* pw = _physics_mut();
    if (r_session.world && pw) {
        r_session.world->view<CharacterStateComponent>([&](Entity, CharacterStateComponent& r_state) {
            pw->destroy_character(r_state.character.body);
            pw->destroy_ragdoll(r_state.character.ragdoll.body);
            r_state.character.ragdoll.body = {};
            r_state.character.ragdoll.state = RagdollState::OFF;
        });
    }
    player = ENTITY_NULL;
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
    r_character.body_low = false;
    r_character.teleport = true;
    r_character.yaw = r_character.target_yaw = r_character.in_air_yaw = r_character.aim_yaw = yaw;
}

const PhysicsWorld* CharacterSystem::_physics() const
{
    return physics && physics->physics.initialized() ? &physics->physics : nullptr;
}

bool CharacterSystem::_low(const Character& p_character)
{
    return p_character.stance == Stance::CROUCHING || p_character.action.active();
}

float CharacterSystem::_height(const Character& p_character, const LocomotionComponent& p_body)
{
    return _low(p_character) ? p_body.crouch_height : p_body.height;
}

float CharacterSystem::_stance_height(const Character& p_character, const LocomotionComponent& p_body)
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

PhysicsWorld* CharacterSystem::_physics_mut() const
{
    return physics && physics->physics.initialized() ? &physics->physics : nullptr;
}

void CharacterSystem::_sync_shape(Character& r_character, const LocomotionComponent& p_body) const
{
    PhysicsWorld* pw = _physics_mut();
    const bool low = _low(r_character);
    if (!pw || !r_character.body.valid() || r_character.body_low == low) return;
    const float height = _height(r_character, p_body);
    if (pw->set_character_shape(r_character.body, std::min(p_body.radius, height * 0.5f - 0.01f), height)) r_character.body_low = low;
    else if (!low) r_character.stance = Stance::CROUCHING;
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
        r_character.body = pw->create_character(r_character.position, std::min(p_body.radius, height * 0.5f - 0.01f), height, std::clamp(p_body.max_slope, MIN_SLOPE_DEGREES, MAX_SLOPE_DEGREES));
        r_character.body_low = _low(r_character);
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
    pw->set_character_max_slope(r_character.body, std::clamp(p_body.max_slope, MIN_SLOPE_DEGREES, MAX_SLOPE_DEGREES));
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
    if (p_input.rotation_toggle) c.desired_rotation_mode = c.desired_rotation_mode == RotationMode::VELOCITY_DIRECTION ? RotationMode::LOOKING_DIRECTION : RotationMode::VELOCITY_DIRECTION;
    c.rotation_mode = p_input.aim ? RotationMode::AIMING : c.desired_rotation_mode;

    const vec3 wish = character_forward(p_aim_yaw) * p_input.move.y + character_right(p_aim_yaw) * p_input.move.x;
    c.input_amount = std::min(length(p_input.move), 1.0f);
    const bool had_input = c.has_input;
    c.has_input = c.input_amount > 0.001f && length(wish) > 1.0e-4f;
    c.stop = had_input && !c.has_input && c.grounded && c.stance == Stance::STANDING && c.speed >= STOP_MIN_SPEED && !c.action.active();
    if (c.has_input) c.input_yaw = character_yaw_of(wish);
    if (p_input.roll && c.grounded && !c.action.active() && p_asset.roll.valid() && c.ragdoll.state == RagdollState::OFF) {
        c.action.start(p_asset.roll, c.has_input ? c.input_yaw : c.yaw, c.velocity);
    }

    const CharacterStance& stance = c.stance == Stance::CROUCHING ? p_asset.crouching : p_asset.standing;
    const bool aiming = c.rotation_mode == RotationMode::AIMING;
    const Gait want = p_input.sprint ? Gait::SPRINTING : Gait::RUNNING;
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
    if (c.stagger_time > 0.0f && c.has_input && length(flat) > 0.1f && dot(normalize(wish), normalize(flat)) > CharacterRagdoll::STAGGER_RELEASE_DOT) c.stagger_time = 0.0f;
    const float stagger = c.stagger_time > 0.0f ? CharacterRagdoll::STAGGER_CONTROL : 1.0f;
    c.stagger_time = std::max(c.stagger_time - p_dt, 0.0f);
    const float rate = (length(target) > length(flat) ? stance.acceleration : braking) * control * stagger;
    c.land_brake_time = std::max(c.land_brake_time - p_dt, 0.0f);
    const vec3 delta = target - flat;
    const float step = rate * p_dt;
    const vec3 next = length(delta) <= step ? target : flat + normalize(delta) * step;
    c.velocity.x = next.x;
    c.velocity.z = next.z;
    if (c.action.active() && c.action.duration > 0.0f) {
        const float momentum = c.action.momentum();
        const float play_rate = 1.0f + (momentum - 1.0f) * CharacterAction::MOMENTUM_RATE;
        const float from = c.action.time;
        const float to = std::min(from + p_dt * play_rate, c.action.duration);
        if (c.action.controlling()) {
            c.yaw = c.target_yaw = character_wrap(c.yaw + character_wrap(c.action.yaw - c.yaw) * std::clamp(p_dt * CharacterAction::TURN_INTERP, 0.0f, 1.0f));
            const vec3 moved = angleAxis(glm::radians(c.yaw + c.model_yaw), UP) * (c.action.root_at(to) - c.action.root_at(from));
            const vec3 root_velocity(moved.x / p_dt, 0.0f, moved.z / p_dt);
            const float root_speed = length(root_velocity);
            const vec3 along = root_speed > 1.0e-3f ? root_velocity / root_speed : character_forward(c.yaw);
            const float progress = c.action.exit_time > 0.0f ? std::clamp(to / c.action.exit_time, 0.0f, 1.0f) : 1.0f;
            const float entry = length(c.action.start_velocity);
            const float carry = entry > c.action.average_speed ? entry * (1.0f - CharacterAction::CARRY_FALLOFF * progress) : 0.0f;
            const float ramp = std::clamp(to / CharacterAction::BLEND_IN, 0.0f, 1.0f);
            const vec3 blended = mix(c.action.start_velocity, along * std::max(root_speed, carry), ramp);
            c.velocity.x = blended.x;
            c.velocity.z = blended.z;
            if (c.has_input && to >= c.action.exit_time) c.action.exit();
        }
        c.action.time = to;
        if (c.action.fade >= 0.0f) c.action.fade += p_dt;
        if (to >= c.action.duration || c.action.fade >= CharacterAction::BLEND_OUT) c.action.stop();
    }
    const float sway = length(c.sway);
    if (c.stagger_time > 0.0f && sway > CharacterRagdoll::SWAY_STEP_ANGLE) {
        const vec3 tip = normalize(cross(c.sway, UP));
        const float need = (sway - CharacterRagdoll::SWAY_STEP_ANGLE) * CharacterRagdoll::SWAY_STEP_SPEED;
        const float along = c.velocity.x * tip.x + c.velocity.z * tip.z;
        if (along < need) {
            c.velocity.x += tip.x * (need - along);
            c.velocity.z += tip.z * (need - along);
        }
    }

    if (p_input.jump && c.grounded && !c.action.controlling()) {
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
    if (!c.grounded) c.velocity.y -= _gravity * p_dt;
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
        const bool saved = p_asset.ragdoll_land_speed <= 0.0f || c.land_speed < p_asset.ragdoll_land_speed + CharacterAction::LAND_ROLL_MARGIN;
        if (p_asset.roll.valid() && c.has_input && p_asset.roll_land_speed > 0.0f && c.land_speed >= p_asset.roll_land_speed && saved && !c.action.active()) {
            const vec3 along(c.velocity.x, 0.0f, c.velocity.z);
            c.action.start(p_asset.roll, length(along) > 0.5f ? character_yaw_of(along) : c.input_yaw, c.velocity);
            c.land_brake_time = 0.0f;
        }
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
        c.look_weight = _interp(c.look_weight, c.action.active() ? 0.0f : weight, p_dt, LOOK_INTERP);
        c.aim_weight = _interp(c.aim_weight, c.rotation_mode == RotationMode::AIMING ? 1.0f : 0.0f, p_dt, AIM_BLEND_INTERP);
    }
    c.speed = length(vec3(c.velocity.x, 0.0f, c.velocity.z));
    c.moving = c.speed > 0.01f;
    if (c.moving) c.velocity_yaw = character_yaw_of(vec3(c.velocity.x, 0.0f, c.velocity.z));
    if (c.speed > stance.run_speed + GAIT_MARGIN) c.gait = allowed == Gait::SPRINTING ? Gait::SPRINTING : Gait::RUNNING;
    else if (c.speed >= stance.walk_speed + GAIT_MARGIN) c.gait = Gait::RUNNING;
    else c.gait = Gait::WALKING;

    _sync_shape(c, p_body);
    if (c.stagger_time <= 0.0f && !c.action.controlling()) _rotate(c, stance, p_dt);
    c.turn_delta = 0.0f;
    c.step_offset = _interp(c.step_offset, 0.0f, p_dt, STEP_SMOOTH);
    const float kill_y = physics && physics->has_floor() ? physics->world_floor - FLOOR_MARGIN : c.spawn_position.y - RESPAWN_DROP;
    if (c.position.y < kill_y) respawn(c);
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

float CharacterSystem::_walkable_cos(const LocomotionComponent& p_body)
{
    return std::cos(glm::radians(std::clamp(p_body.max_slope, MIN_SLOPE_DEGREES, MAX_SLOPE_DEGREES)));
}

bool CharacterSystem::_ground_at(const vec3& p_position, float p_top, float p_walkable_cos, PhysicsHit& r_hit) const
{
    const PhysicsWorld* pw = _physics();
    if (!pw) return false;
    const vec3 origin(p_position.x, p_top, p_position.z);
    const vec3 end = origin - UP * (FootIK::TRACE_ABOVE + FootIK::TRACE_BELOW);
    bool found = pw->sphere_cast(origin, FootIK::TRACE_RADIUS, -UP, FootIK::TRACE_ABOVE + FootIK::TRACE_BELOW, r_hit);
    if (found && r_hit.normal.y < p_walkable_cos) {
        PhysicsHit ray;
        found = pw->raycast(origin, -UP, FootIK::TRACE_ABOVE + FootIK::TRACE_BELOW + FootIK::TRACE_RADIUS, ray) && ray.normal.y >= p_walkable_cos;
        if (found) r_hit = ray;
    }
    if (!_debug) return found;
    DebugDraw& draw = debug_draw();
    draw.line(origin, found ? r_hit.position : end, DebugDraw::Style{ .color = found ? DebugDraw::YELLOW : DebugDraw::RED });
    if (found) draw.line(r_hit.position, r_hit.position + r_hit.normal * 0.1f, DebugDraw::Style{ .color = DebugDraw::CYAN });
    return found;
}

void CharacterSystem::_update_foot_ik(Character& r_character, const LLocomotion& p_asset, const LocomotionComponent& p_body, float p_dt) const
{
    const float model_yaw = p_body.model_yaw;
    const float walkable = _walkable_cos(p_body);
    FootIK& ik = r_character.ik;
    const bool active = p_asset.foot_ik && ik.valid && r_character.grounded && _physics();
    ik.alpha = _interp(ik.alpha, active ? 1.0f : 0.0f, p_dt, FootIK::ALPHA_INTERP);
    const quat rotation = angleAxis(glm::radians(r_character.yaw + model_yaw), UP);
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
    const bool turning = an.turn_time >= 0.0f || an.rotate_weight > BLEND_MIN_WEIGHT;

    for (int i = 0; i < 2; i++) {
        const float height = ik.model[i].y - ik.rest_height[i];
        const float vertical = ik.started ? (ik.model[i].y - ik.previous_height[i]) / p_dt : 0.0f;
        ik.previous_height[i] = ik.model[i].y;
        const vec3 foot = root + rotation * ik.model[i];
        const vec3 moved = foot - ik.previous_world[i];
        const float ground_speed = ik.started ? length(vec3(moved.x, 0.0f, moved.z)) / p_dt : 0.0f;
        ik.previous_world[i] = foot;
        if (ik.planted[i]) ik.planted[i] = height < FootIK::UNPLANT_HEIGHT && std::fabs(vertical) < FootIK::UNPLANT_VERTICAL_SPEED && ground_speed < FootIK::UNPLANT_GROUND_SPEED;
        else ik.planted[i] = height < FootIK::PLANT_HEIGHT && std::fabs(vertical) < FootIK::PLANT_VERTICAL_SPEED && ground_speed < FootIK::PLANT_GROUND_SPEED;

        float target = 0.0f;
        quat tilt(1.0f, 0.0f, 0.0f, 0.0f);
        PhysicsHit heel;
        if (active && _ground_at(foot, root.y + FootIK::TRACE_ABOVE, walkable, heel)) {
            target = std::clamp(heel.position.y - root.y, -FootIK::MAX_DROP, FootIK::MAX_RAISE);
            const vec3 side = normalize(vec3(heel.normal.x, 0.0f, heel.normal.z) - forward * dot(vec3(heel.normal.x, 0.0f, heel.normal.z), forward) + vec3(0.0f, heel.normal.y, 0.0f));
            tilt = normalize(quat(1.0f + dot(UP, side), cross(UP, side).x, cross(UP, side).y, cross(UP, side).z));
            PhysicsHit ball;
            if (_ground_at(foot + forward * FootIK::BALL_DISTANCE, root.y + FootIK::TRACE_ABOVE, walkable, ball)) {
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
        if (!ik.planted[i]) ik.released[i] = false;
        if (active && ik.planted[i] && !ik.locked[i] && !ik.released[i] && !turning) {
            ik.locked[i] = true;
            ik.lock_world[i] = placed;
            ik.lock_alpha[i] = 0.0f;
            ik.slide_from[i] = ik.slide[i];
        }
        if (ik.locked[i]) {
            const vec3 drift(ik.lock_world[i].x - placed.x, 0.0f, ik.lock_world[i].z - placed.z);
            if (still && r_character.dynamic_foot < 0 && r_character.dynamic_cooldown <= 0.0f && length(drift) > DYNAMIC_THRESHOLD) {
                r_character.dynamic_foot = i;
                r_character.dynamic_cooldown = DYNAMIC_RETRIGGER;
                ik.locked[i] = false;
                ik.released[i] = true;
            } else if (!active || !ik.planted[i] || turning || length(drift) > FootIK::MAX_LOCK_DRIFT) {
                ik.locked[i] = false;
                ik.released[i] = ik.planted[i];
            }
        }
        const vec3 lock(ik.lock_world[i].x - placed.x, 0.0f, ik.lock_world[i].z - placed.z);
        if (ik.locked[i]) {
            ik.lock_alpha[i] = _interp(ik.lock_alpha[i], 1.0f, p_dt, FootIK::LOCK_IN);
            ik.slide[i] = mix(ik.slide_from[i], lock, ik.lock_alpha[i]);
        } else {
            ik.lock_alpha[i] = 0.0f;
            ik.slide[i] = _interp(ik.slide[i], vec3(0.0f), p_dt, FootIK::LOCK_OUT);
        }
        ik.world_offset[i] = UP * ik.height_offset[i] + ik.slide[i];
        if (active && _debug) {
            DebugDraw& draw = debug_draw();
            draw.point(placed + ik.slide[i], 6.0f, DebugDraw::Style{ .color = ik.planted[i] ? DebugDraw::GREEN : DebugDraw::BLUE });
            if (ik.locked[i]) {
                draw.sphere(ik.lock_world[i], 0.03f, DebugDraw::Style{ .color = DebugDraw::GREEN });
                draw.line(ik.lock_world[i], placed, DebugDraw::Style{ .color = DebugDraw::ORANGE });
            }
        }
    }

    const float pelvis = active ? std::max(std::min(ik.height_offset[0], ik.height_offset[1]), -FootIK::MAX_PELVIS_DROP) : 0.0f;
    _spring(ik.pelvis_offset, ik.pelvis_velocity, std::min(pelvis, 0.0f), FootIK::PELVIS_SPRING, p_dt);
    if (active && _debug && std::fabs(ik.pelvis_offset) > 0.005f) {
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

bool CharacterSystem::_detect_impact(Character& r_character, const LLocomotion& p_asset, const LocomotionComponent& p_body, float p_dt) const
{
    const PhysicsWorld* pw = _physics();
    if (!pw || !p_asset.ragdoll || !r_character.ragdoll.valid) return false;
    const float height = _height(r_character, p_body);
    const float radius = std::min(p_body.radius, height * 0.5f - 0.01f);
    const bool air = r_character.movement == MovementState::IN_AIR;
    CharacterRagdoll& rag = r_character.ragdoll;

    const auto take = [&](const PhysicsHit& p_hit, const vec3& p_dir, float p_speed, float p_push, float p_scale, bool p_soft) {
        if (pw->body_mass(p_hit.body) < IMPACT_MIN_MASS) return false;
        const bool repeat = rag.obstacle_memory > 0.0f && length(p_hit.position - rag.last_obstacle) < CharacterRagdoll::OBSTACLE_RADIUS;
        rag.last_obstacle = p_hit.position;
        rag.obstacle_memory = CharacterRagdoll::OBSTACLE_MEMORY;
        if (repeat && p_soft) return false;
        rag.hit_point = p_hit.position;
        rag.hit_direction = p_dir;
        rag.hit_speed = p_speed;
        rag.hit_push = p_push;
        rag.hit_scale = std::clamp(p_scale, CharacterRagdoll::MIN_FLINCH_SCALE, 1.0f);
        rag.hit_soft = p_soft;
        return true;
    };
    const auto wall = [&](const PhysicsHit& p_hit, const vec3& p_dir) {
        return std::fabs(p_hit.normal.y) < WALL_MAX_NORMAL_Y && dot(p_hit.normal, p_dir) < IMPACT_FACING;
    };

    const vec3 flat(r_character.velocity.x, 0.0f, r_character.velocity.z);
    const float speed = length(flat);
    const vec3 dir = speed > 1.0e-4f ? flat / speed : vec3(0.0f);
    const float reach = radius + speed * p_dt * 2.0f + IMPACT_REACH;
    PhysicsHit hit;

    if (air) {
        const float rise = r_character.velocity.y;
        if (rise > AIR_RISE_SPEED) {
            const vec3 top = r_character.position + UP * (height - radius);
            if (pw->sphere_cast(top, IMPACT_PROBE_RADIUS, UP, radius + rise * p_dt * 2.0f + IMPACT_REACH, hit)) return take(hit, UP, std::max(rise, speed), rise * CharacterRagdoll::OBSTACLE_PUSH, 1.0f, false);
        }
        if (speed < AIR_FLINCH_SPEED) return false;
        const float heights[3] = { height - HEAD_PROBE_DROP, height * CHEST_PROBE_HEIGHT, height * LEG_PROBE_HEIGHT };
        bool found = false;
        bool upper = false;
        PhysicsHit best;
        for (uint32_t i = 0; i < 3; i++) {
            if (!pw->sphere_cast(r_character.position + UP * heights[i], IMPACT_PROBE_RADIUS, dir, reach, hit) || !wall(hit, dir)) continue;
            if (!found || hit.distance < best.distance) best = hit;
            found = true;
            upper = upper || i < 2;
        }
        return found && take(best, dir, speed, speed * CharacterRagdoll::OBSTACLE_PUSH, upper ? 1.0f : 0.4f, !upper);
    }

    if (speed < IMPACT_MIN_SPEED) return false;
    const auto stumble = [&](const PhysicsHit& p_hit, float p_min_speed) {
        const vec3 normal(p_hit.normal.x, 0.0f, p_hit.normal.z);
        if (length(normal) < 1.0e-3f) return false;
        const vec3 n = normalize(normal);
        const float facing = -dot(n, dir);
        if (facing < STUMBLE_FACING) return false;
        const float head_on = std::clamp((facing - STUMBLE_FACING) / (1.0f - STUMBLE_FACING), 0.0f, 1.0f);
        const float into = speed * facing;
        if (into < p_min_speed) return false;
        const float bounce = std::clamp(into * CharacterRagdoll::GROUND_BOUNCE, CharacterRagdoll::MIN_BOUNCE, CharacterRagdoll::MAX_BOUNCE) * head_on;
        const float scale = std::clamp(into / CharacterRagdoll::STUMBLE_REFERENCE_SPEED, CharacterRagdoll::MIN_HIT_SCALE, 1.0f) * head_on;
        return take(p_hit, -n, into, bounce, scale, true);
    };
    PhysicsHit low;
    const bool blocked = pw->sphere_cast(r_character.position + UP * (height * BODY_PROBE_HEIGHT), IMPACT_PROBE_RADIUS, dir, reach, low);
    if (!blocked && p_asset.head_hit_speed > 0.0f) {
        if (pw->sphere_cast(r_character.position + UP * (height - HEAD_PROBE_DROP), IMPACT_PROBE_RADIUS, dir, reach, hit)) return stumble(hit, p_asset.head_hit_speed);
    }
    if (blocked && wall(low, dir)) {
        PhysicsHit chest;
        const bool upper = pw->sphere_cast(r_character.position + UP * (height * CHEST_PROBE_HEIGHT), IMPACT_PROBE_RADIUS, dir, reach, chest) && wall(chest, dir);
        return stumble(upper ? chest : low, WALL_FLINCH_SPEED);
    }
    return false;
}

float CharacterSystem::_random() const
{
    _seed ^= _seed << 13;
    _seed ^= _seed >> 17;
    _seed ^= _seed << 5;
    return (float)(_seed & 0xFFFFFFu) / (float)0x1000000u;
}

bool CharacterSystem::_air_catch(const Character& p_character, float p_limit) const
{
    const CharacterRagdoll& rag = p_character.ragdoll;
    const float severity = std::clamp((rag.hit_speed - p_limit) / CharacterRagdoll::AIR_CATCH_RANGE, 0.0f, 1.0f);
    const float gap = p_character.ground_gap;
    const float near = gap >= 0.0f ? std::clamp(1.0f - gap / CharacterRagdoll::AIR_CATCH_HEIGHT, CharacterRagdoll::AIR_CATCH_FAR, 1.0f) : CharacterRagdoll::AIR_CATCH_FAR;
    float chance = CharacterRagdoll::AIR_CATCH_CHANCE * (1.0f - severity) * near;
    if (rag.hit_direction.y > 0.9f) chance *= CharacterRagdoll::AIR_CATCH_CEILING;
    return _random() < chance;
}

void CharacterSystem::_apply_hit(Character& r_character, const LLocomotion& p_asset) const
{
    CharacterRagdoll& rag = r_character.ragdoll;
    rag.hit_pending = false;
    if (!p_asset.ragdoll || rag.state != RagdollState::OFF) {
        rag.hit_speed = rag.hit_push = 0.0f;
        return;
    }
    const bool air = r_character.movement == MovementState::IN_AIR;
    const float limit = air ? p_asset.air_ragdoll_speed : p_asset.balance;
    bool falls = !rag.hit_soft && limit > 0.0f && rag.hit_speed >= limit;
    if (falls && air && _air_catch(r_character, limit)) {
        falls = false;
        rag.land_stumble = true;
    }
    rag.hit_soft = false;
    if (falls) _start_ragdoll(r_character, p_asset);
    else _stumble(r_character);
}

void CharacterSystem::_stumble(Character& r_character) const
{
    CharacterRagdoll& rag = r_character.ragdoll;
    const vec3 direction = rag.hit_direction;
    const float push = rag.hit_push;
    const float scale = std::clamp(rag.hit_scale, 0.0f, 1.0f);
    rag.hit_speed = rag.hit_push = 0.0f;
    const vec3 flat_dir(direction.x, 0.0f, direction.z);
    if (length(flat_dir) <= 1.0e-3f) return;
    const vec3 d = normalize(flat_dir);
    vec3 flat(r_character.velocity.x, 0.0f, r_character.velocity.z);
    const float into = std::max(dot(flat, d), 0.0f);
    flat -= d * (into + push * CharacterRagdoll::SHOVE_SCALE);
    r_character.velocity.x = flat.x;
    r_character.velocity.z = flat.z;
    r_character.stagger_time = CharacterRagdoll::STAGGER_MIN + CharacterRagdoll::STAGGER_RANGE * scale;
    _impact_body(r_character, -d * (into + push));
}

void CharacterSystem::_impact_body(Character& r_character, const vec3& p_impulse)
{
    const CharacterRagdoll& rag = r_character.ragdoll;
    const float strength = length(p_impulse);
    if (strength < 1.0e-3f) return;
    const float com = rag.has_current ? (rag.current_matrix * rag.current[(uint32_t)RagdollJoint::PELVIS])[3].y : r_character.position.y + CharacterRagdoll::SWAY_PIVOT_HEIGHT;
    const float lever = rag.hit_point.y - com;

    vec3 spin = cross(vec3(0.0f, lever, 0.0f), p_impulse) * (CharacterRagdoll::SWAY_GAIN / CharacterRagdoll::SWAY_LEVER);
    if (length(spin) > CharacterRagdoll::SWAY_MAX_SPIN) spin = normalize(spin) * CharacterRagdoll::SWAY_MAX_SPIN;
    r_character.sway_velocity += spin;
}

void CharacterSystem::_update_sway(Character& r_character, float p_dt)
{
    const float omega = glm::two_pi<float>() * CharacterRagdoll::SWAY_FREQUENCY;
    const vec3 accel = -r_character.sway * (omega * omega) - r_character.sway_velocity * (2.0f * CharacterRagdoll::SWAY_DAMPING * omega);
    r_character.sway_velocity += accel * p_dt;
    r_character.sway += r_character.sway_velocity * p_dt;
    r_character.sway.y = r_character.sway_velocity.y = 0.0f;
    const float angle = length(r_character.sway);
    if (angle > CharacterRagdoll::SWAY_MAX) {
        r_character.sway *= CharacterRagdoll::SWAY_MAX / angle;
        r_character.sway_velocity -= (r_character.sway / CharacterRagdoll::SWAY_MAX) * std::max(dot(r_character.sway_velocity, r_character.sway / CharacterRagdoll::SWAY_MAX), 0.0f);
    }
    if (angle < 1.0e-4f && length(r_character.sway_velocity) < 1.0e-3f) r_character.sway = r_character.sway_velocity = vec3(0.0f);
}

void CharacterSystem::_start_ragdoll(Character& r_character, const LLocomotion& p_asset) const
{
    PhysicsWorld* pw = _physics_mut();
    CharacterRagdoll& rag = r_character.ragdoll;
    if (!pw || !p_asset.ragdoll) return;
    PhysicsRagdollDesc desc;
    const bool built = rag.build(p_asset, r_character.model_yaw, r_character.velocity, desc);
    rag.hit_speed = 0.0f;
    if (!built) return;
    rag.body = pw->create_ragdoll(desc);
    if (!rag.body.valid()) return;
    rag.strength = p_asset.muscle_strength;
    rag.update_muscles(p_asset, RagdollPose::HIT, 0.0f);
    pw->drive_ragdoll(rag.body, rag.muscle_target, rag.muscle, RAGDOLL_PART_COUNT, CharacterRagdoll::MUSCLE_FREQUENCY);
    rag.state = RagdollState::ACTIVE;
    rag.land_stumble = false;
    r_character.action.stop();
    rag.time = 0.0f;
    rag.still_time = 0.0f;
    rag.can_get_up = false;
    rag.step = pw->steps();
    rag.previous_positions.clear();
    rag.previous_rotations.clear();
    pw->read_ragdoll(rag.body, rag.positions, rag.rotations, &rag.velocities);
}

void CharacterSystem::_update_ragdoll(Character& r_character, const LLocomotion& p_asset, bool p_toggle, bool p_wants_up, float p_dt) const
{
    PhysicsWorld* pw = _physics_mut();
    CharacterRagdoll& rag = r_character.ragdoll;
    if (!pw || rag.positions.size() != RAGDOLL_PART_COUNT) {
        _end_ragdoll(r_character, p_asset);
        return;
    }
    rag.time += p_dt;
    if (pw->steps() != rag.step) {
        rag.previous_positions = rag.positions;
        rag.previous_rotations = rag.rotations;
        if (!pw->read_ragdoll(rag.body, rag.positions, rag.rotations, &rag.velocities) || rag.positions.size() != RAGDOLL_PART_COUNT) {
            _end_ragdoll(r_character, p_asset);
            return;
        }
        rag.step = pw->steps();
    }
    vec3 head(0.0f, 1.0f, 0.0f);
    rag.update_facing(r_character.model_yaw, head);
    const vec3 flat(head.x, 0.0f, head.z);
    if (length(flat) > 0.2f) r_character.yaw = character_wrap(character_yaw_of(flat) + (rag.face_up ? 180.0f : 0.0f));
    r_character.target_yaw = r_character.in_air_yaw = r_character.velocity_yaw = r_character.yaw;

    const vec3 pelvis = rag.positions[0];
    PhysicsHit hit;
    const bool ground = pw->raycast(pelvis + UP * 0.1f, -UP, CharacterRagdoll::GROUND_PROBE, hit);
    r_character.position = ground ? hit.position : pelvis - UP * CharacterRagdoll::AIR_PELVIS_HEIGHT;
    r_character.velocity = vec3(0.0f);
    r_character.speed = 0.0f;
    r_character.moving = false;
    r_character.step_offset = 0.0f;

    float speed = 0.0f;
    for (const vec3& v : rag.velocities) speed += length(v);
    speed /= (float)std::max<size_t>(rag.velocities.size(), 1);
    const bool resting = ground && hit.distance < CharacterRagdoll::AIR_PELVIS_HEIGHT * 0.6f;
    rag.still_time = speed < CharacterRagdoll::SETTLE_SPEED && resting ? rag.still_time + p_dt : 0.0f;

    RagdollPose pose = RagdollPose::RELAX;
    if (rag.time < CharacterRagdoll::HIT_TIME) {
        pose = RagdollPose::HIT;
    } else if (!ground || hit.distance > CharacterRagdoll::BRACE_HEIGHT) {
        const vec3 v = rag.velocities.empty() ? vec3(0.0f) : rag.velocities[0];
        const vec3 flat_v(v.x, 0.0f, v.z);
        const vec3 flat_front(rag.front.x, 0.0f, rag.front.z);
        const bool backward = length(flat_v) > 0.5f && length(flat_front) > 0.1f && dot(normalize(flat_v), normalize(flat_front)) < CharacterRagdoll::BRACE_BACK_DOT;
        pose = backward ? RagdollPose::BRACE_BACK : RagdollPose::BRACE;
    } else if (speed > CharacterRagdoll::PROTECT_SPEED) {
        pose = RagdollPose::PROTECT;
    }
    rag.update_muscles(p_asset, pose, p_dt);
    pw->drive_ragdoll(rag.body, rag.muscle_target, rag.muscle, RAGDOLL_PART_COUNT, CharacterRagdoll::MUSCLE_FREQUENCY);

    const bool settled = rag.time > CharacterRagdoll::MIN_TIME && rag.still_time > CharacterRagdoll::SETTLE_TIME;
    if (settled || rag.time > CharacterRagdoll::MAX_TIME) rag.can_get_up = true;
    if ((rag.can_get_up && p_wants_up) || (p_toggle && rag.time > CharacterRagdoll::TOGGLE_MIN_TIME)) _end_ragdoll(r_character, p_asset);
}

void CharacterSystem::_end_ragdoll(Character& r_character, const LLocomotion& p_asset) const
{
    CharacterRagdoll& rag = r_character.ragdoll;
    if (PhysicsWorld* pw = _physics_mut()) pw->destroy_ragdoll(rag.body);
    rag.body = {};
    rag.state = RagdollState::GETTING_UP;
    rag.get_up_clip = rag.face_up ? p_asset.get_up_back : p_asset.get_up_front;
    rag.get_up_time = 0.0f;
    rag.previous_positions.clear();
    rag.previous_rotations.clear();

    r_character.anim = CharacterAnim{};
    r_character.ik.started = false;
    r_character.movement = MovementState::GROUNDED;
    r_character.grounded = true;
    r_character.jumped = false;
    r_character.landed = false;
    r_character.jump_flight = false;
    r_character.velocity = vec3(0.0f);
    r_character.acceleration = vec3(0.0f);
    r_character.speed = 0.0f;
    r_character.moving = false;
    r_character.land_speed = 0.0f;
    r_character.land_brake_time = 0.0f;
    r_character.ungrounded_time = 0.0f;
    r_character.ground_gap = -1.0f;
    r_character.step_offset = 0.0f;
    r_character.lean_lr = r_character.lean_fb = 0.0f;
    r_character.target_yaw = r_character.in_air_yaw = r_character.velocity_yaw = r_character.aim_yaw = r_character.yaw;
    r_character.teleport = true;
}

void CharacterSystem::_draw_ragdoll(const Character& p_character) const
{
    const CharacterRagdoll& rag = p_character.ragdoll;
    if (rag.state != RagdollState::ACTIVE) return;
    DebugDraw& draw = debug_draw();
    const uint32_t colors[RAGDOLL_POSE_COUNT] = { DebugDraw::RED, DebugDraw::YELLOW, DebugDraw::YELLOW, DebugDraw::ORANGE, DebugDraw::GREEN };
    const uint32_t color = colors[std::min((uint32_t)rag.muscle_pose, RAGDOLL_POSE_COUNT - 1)];
    const uint32_t count = std::min((uint32_t)rag.positions.size(), RAGDOLL_PART_COUNT);
    for (uint32_t i = 0; i < count; i++) {
        const vec3 a = rag.positions[i] + rag.rotations[i] * rag.shape_a[i];
        const vec3 b = rag.positions[i] + rag.rotations[i] * rag.shape_b[i];
        draw.capsule(a, b, rag.shape_radius[i], DebugDraw::Style{ .color = color });
    }
    draw.sphere(rag.pelvis, 0.04f, DebugDraw::Style{ .color = rag.face_up ? DebugDraw::CYAN : DebugDraw::RED });
    draw.line(rag.pelvis, p_character.position, DebugDraw::Style{ .color = DebugDraw::YELLOW });
}

void CharacterSystem::update(GameSession& r_session, float p_dt)
{
    if (!r_session.world || p_dt <= 0.0f || !anims) return;
    Scene& world = *r_session.world;
    _gravity = world.physics.gravity;

    _entities.clear();
    world.view<LocomotionComponent, TransformComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const LocomotionComponent&, const TransformComponent&) { _entities.push_back(p_entity); });
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
        if (world.has<StaticTag>(e)) world.remove<StaticTag>(e);
    }

    if (!world.valid(player) || !world.has<PlayerTag>(player) || !world.has<CharacterStateComponent>(player)) {
        player = ENTITY_NULL;
        for (Entity e : _entities) {
            if (!world.has<PlayerTag>(e)) continue;
            player = e;
            break;
        }
    }

    const GameInput idle;
    const vec3 view = r_session.camera.rotation * vec3(0.0f, 0.0f, -1.0f);
    const float view_yaw = character_yaw_of(view);
    const float view_pitch = glm::degrees(std::asin(std::clamp(view.y, -1.0f, 1.0f)));
    for (Entity e : _entities) {
        CharacterStateComponent* state = world.try_get<CharacterStateComponent>(e);
        const LocomotionComponent* body = world.try_get<LocomotionComponent>(e);
        if (!state || !body) continue;
        Character& c = state->character;
        const bool listed = debug_all || std::find(debug_entities.begin(), debug_entities.end(), e) != debug_entities.end();
        _debug = listed && debug_lines;
        c.asset = body->locomotion;
        c.model_yaw = body->model_yaw;
        const LLocomotion* asset = anims->locomotion(c.asset);
        if (!asset) continue;

        const bool is_player = e == player;
        CharacterRagdoll& rag = c.ragdoll;
        rag.obstacle_memory = std::max(rag.obstacle_memory - p_dt, 0.0f);
        const bool toggle = is_player && r_session.input.ragdoll_toggle;
        if (is_player && (r_session.input.hit_light || r_session.input.hit_heavy || r_session.input.hit_low)) {
            const vec3 push = character_forward(view_yaw);
            const vec3 point = c.position + UP * (_height(c, *body) * (r_session.input.hit_low ? TEST_HIT_LOW_HEIGHT : TEST_HIT_HEIGHT)) - push * 0.1f;
            rag.hit(point, push, r_session.input.hit_heavy ? TEST_HIT_HEAVY : TEST_HIT_LIGHT);
        }
        if (rag.state == RagdollState::OFF && toggle) {
            _start_ragdoll(c, *asset);
        } else if (rag.hit_pending) {
            _apply_hit(c, *asset);
        } else if (rag.state == RagdollState::OFF && _detect_impact(c, *asset, *body, p_dt)) {
            _apply_hit(c, *asset);
        }
        const bool controllable = rag.state == RagdollState::OFF;
        if (rag.state == RagdollState::ACTIVE) {
            const GameInput& in = r_session.input;
            const bool wants_up = !is_player || length(in.move) > GameInput::STICK_DEADZONE || in.jump || in.roll || in.crouch_toggle || in.sprint || in.aim;
            _update_ragdoll(c, *asset, toggle, wants_up, p_dt);
        }
        else if (is_player && controllable) _simulate(c, *asset, *body, r_session.input, view_yaw, view_pitch, p_dt);
        else _simulate(c, *asset, *body, idle, c.yaw, 0.0f, p_dt);
        if (controllable && c.landed && !c.action.active() && asset->ragdoll_land_speed > 0.0f && c.land_speed >= asset->ragdoll_land_speed) {
            _start_ragdoll(c, *asset);
        }
        if (rag.land_stumble && c.landed && rag.state != RagdollState::ACTIVE) {
            rag.land_stumble = false;
            c.stagger_time = CharacterRagdoll::STAGGER_MIN + CharacterRagdoll::STAGGER_RANGE;
        }
        if (rag.state == RagdollState::OFF) _update_sway(c, p_dt);
        else c.sway = c.sway_velocity = vec3(0.0f);

        const TransformComponent current = world.world_transform(e);
        const mat4 m = translate(mat4(1.0f), c.position + UP * c.step_offset) * mat4_cast(angleAxis(glm::radians(c.yaw + body->model_yaw), UP)) * scale(mat4(1.0f), current.scale);
        world.set_world_matrix(e, m);
        rag.matrix = m;
        if (rag.state == RagdollState::ACTIVE) {
            const PhysicsWorld* pw = _physics();
            rag.update_driven(pw ? pw->interpolation_alpha() : 1.0f);
            if (_debug) _draw_ragdoll(c);
        }
        if (_debug) {
            DebugDraw& draw = debug_draw();
            const vec3 base = c.position + UP * 0.05f;
            draw.arrow(base, base + character_forward(c.yaw) * 0.6f, DebugDraw::Style{ .color = DebugDraw::BLUE });
            const vec3 flat(c.velocity.x, 0.0f, c.velocity.z);
            if (length(flat) > 0.05f) draw.arrow(base, base + flat * 0.25f, DebugDraw::Style{ .color = DebugDraw::GREEN });
            if (c.has_input) draw.arrow(base, base + character_forward(c.input_yaw) * c.input_amount, DebugDraw::Style{ .color = DebugDraw::YELLOW });
        }
        if (rag.state == RagdollState::OFF) _update_foot_ik(c, *asset, *body, p_dt);
    }
}

}
