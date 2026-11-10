#include <core/game/character_system.h>
#include <core/base/hash.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <cstring>
#include <vector>

namespace lumen {

static uint64_t _hash_float(uint64_t p_hash, float p_value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &p_value, sizeof(bits));
    return hash_combine(p_hash, bits);
}

static float _yaw_of(const vec3& p_forward)
{
    return degrees(std::atan2(-p_forward.x, -p_forward.z));
}

static vec3 _move_towards(const vec3& p_from, const vec3& p_to, float p_max)
{
    const vec3 delta = p_to - p_from;
    const float len = length(delta);
    return len <= p_max || len <= 0.0f ? p_to : p_from + delta / len * p_max;
}

static float _turn_towards(float p_from, float p_to, float p_max)
{
    const float delta = std::remainder(p_to - p_from, 360.0f);
    if (std::abs(delta) <= p_max) return p_to;
    return p_from + (delta > 0.0f ? p_max : -p_max);
}

uint64_t CharacterSystem::settings_hash(const CharacterComponent& p_character)
{
    return _hash_float(_hash_float(_hash_float(0, p_character.radius), p_character.height), p_character.max_slope);
}

void CharacterSystem::start(GameSession& r_session)
{
    (void)r_session;
    _clear();
}

void CharacterSystem::stop(GameSession& r_session)
{
    (void)r_session;
    _clear();
}

void CharacterSystem::_clear()
{
    if (physics) {
        for (auto& [k, t] : characters) physics->physics.destroy_character(t.id);
    }
    characters.clear();
}

void CharacterSystem::update(GameSession& r_session, float p_dt)
{
    if (!r_session.world || !physics || !physics->physics.initialized() || p_dt <= 0.0f) return;
    World& world = *r_session.world;
    PhysicsWorld& pw = physics->physics;

    for (auto& [k, t] : characters) t.seen = false;
    std::vector<Entity> entities;
    world.view<TransformComponent, CharacterComponent>([&](Entity p_entity, const TransformComponent&, const CharacterComponent&) { entities.push_back(p_entity); });

    for (const Entity e : entities) {
        if (!world.has<CharacterStateComponent>(e)) world.add<CharacterStateComponent>(e);
        const CharacterComponent c = *world.try_get<CharacterComponent>(e);
        CharacterStateComponent& state = *world.try_get<CharacterStateComponent>(e);
        const mat4 matrix = world.world_matrix(e);
        const vec3 position = vec3(matrix[3]);
        const vec3 scale(length(vec3(matrix[0])), length(vec3(matrix[1])), length(vec3(matrix[2])));
        const uint64_t k = PhysicsSystem::key(e);
        const uint64_t hash = settings_hash(c);

        auto it = characters.find(k);
        if (it != characters.end() && it->second.hash != hash) {
            pw.destroy_character(it->second.id);
            characters.erase(it);
            it = characters.end();
        }
        if (it == characters.end()) {
            PhysicsCharacterDesc desc;
            desc.radius = c.radius;
            desc.height = c.height;
            desc.max_slope = c.max_slope;
            desc.position = position;
            desc.user_data = k;
            Tracked t;
            t.entity = e;
            t.hash = hash;
            t.id = pw.create_character(desc);
            t.written = position;
            if (!t.id.valid()) continue;
            it = characters.emplace(k, t).first;
            state = CharacterStateComponent{};
            state.facing = _yaw_of(vec3(matrix * vec4(0.0f, 0.0f, -1.0f, 0.0f))) - c.yaw_offset;
        } else if (distance(position, it->second.written) > TELEPORT_EPSILON) {
            pw.set_character_position(it->second.id, position);
            state.velocity = vec3(0.0f);
        }
        Tracked& t = it->second;
        t.seen = true;

        const float yaw = radians(state.has_move_yaw ? state.move_yaw : state.facing);
        const vec3 forward(-std::sin(yaw), 0.0f, -std::cos(yaw));
        const vec3 right(std::cos(yaw), 0.0f, -std::sin(yaw));

        const vec2 move = state.move;
        const vec3 wish = right * move.x + forward * move.y;
        const float amount = std::min(length(wish), 1.0f);
        const bool moving = amount > MOVE_DEADZONE;
        const vec3 direction = moving ? normalize(wish) : vec3(0.0f);
        const bool walking = state.walk;
        const bool sprinting = moving && !walking && state.run;
        const float top_speed = walking ? c.walk_speed : (sprinting ? c.sprint_speed : c.jog_speed);
        const vec3 target = direction * (top_speed * amount);

        vec3 horizontal(state.velocity.x, 0.0f, state.velocity.z);
        const float rate = state.on_ground ? (length(target) >= length(horizontal) ? c.acceleration : c.deceleration) : c.acceleration * c.air_control;
        horizontal = _move_towards(horizontal, target, rate * p_dt);

        float vertical = state.velocity.y;
        bool jumped = false;
        if (state.on_ground) {
            vertical = 0.0f;
            if (state.jump) {
                vertical = c.jump_speed;
                jumped = true;
            }
        } else {
            vertical += pw.gravity().y * c.gravity_scale * p_dt;
        }

        state.jump = false;
        const bool grounded = state.on_ground && !jumped;
        PhysicsCharacterState result;
        if (!pw.update_character(t.id, vec3(horizontal.x, vertical, horizontal.z), p_dt, grounded ? c.step_height : 0.0f, grounded ? STICK_TO_FLOOR : 0.0f, result)) continue;

        state.velocity = result.velocity;
        if (jumped) state.velocity.y = std::max(state.velocity.y, vertical);
        state.on_ground = result.on_ground && !jumped;
        state.sprinting = sprinting;
        state.speed = length(vec2(result.velocity.x, result.velocity.z));
        if (moving) state.facing = _turn_towards(state.facing, _yaw_of(direction), c.turn_speed * p_dt);

        const mat4 out = translate(mat4(1.0f), result.position) * mat4_cast(angleAxis(radians(state.facing + c.yaw_offset), vec3(0.0f, 1.0f, 0.0f))) * glm::scale(mat4(1.0f), scale);
        world.set_world_matrix(e, out);
        t.written = result.position;

        if (AnimationComponent* anim = world.try_get<AnimationComponent>(e)) {
            anim->speed = state.speed;
            anim->direction = 0.0f;
        }
    }

    for (auto it = characters.begin(); it != characters.end();) {
        if (it->second.seen) { ++it; continue; }
        pw.destroy_character(it->second.id);
        it = characters.erase(it);
    }
}

}
