#include <core/game/orbit_camera_system.h>
#include <core/game/physics_system.h>
#include <core/game/character_system.h>
#include <core/game/character.h>
#include <core/scene/components.h>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace lumen {

static const vec3 ORBIT_UP(0.0f, 1.0f, 0.0f);

static float _interp(float p_current, float p_target, float p_dt, float p_speed)
{
    return p_current + (p_target - p_current) * std::clamp(p_dt * p_speed, 0.0f, 1.0f);
}

static vec3 _look(float p_yaw, float p_pitch)
{
    const float yaw = glm::radians(p_yaw);
    const float pitch = glm::radians(p_pitch);
    return vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
}

const PhysicsWorld* OrbitCameraSystem::_physics() const
{
    return physics && physics->physics.initialized() ? &physics->physics : nullptr;
}

void OrbitCameraSystem::start(GameSession& r_session)
{
    base_fov = fov = r_session.camera.fov_y;
    target = ENTITY_NULL;
    ready = false;
}

void OrbitCameraSystem::stop(GameSession& r_session)
{
    (void)r_session;
    target = ENTITY_NULL;
    ready = false;
}

vec3 OrbitCameraSystem::_pivot_target(Scene& p_world) const
{
    const OrbitCameraComponent& cam = *p_world.try_get<OrbitCameraComponent>(target);
    const CharacterStateComponent* state = p_world.try_get<CharacterStateComponent>(target);
    const CharacterComponent* body = p_world.try_get<CharacterComponent>(target);
    if (state && body) {
        const Character& c = state->character;
        return c.visual_position() + ORBIT_UP * (CharacterSystem::_stance_height(c, *body) * cam.pivot_height);
    }
    return p_world.world_transform(target).position + ORBIT_UP * cam.pivot_height;
}

void OrbitCameraSystem::update(GameSession& r_session, float p_dt)
{
    (void)p_dt;
    if (!r_session.world) return;
    Scene& world = *r_session.world;
    Entity best = ENTITY_NULL;
    bool best_player = false;
    world.view<OrbitCameraComponent>(Exclude<DisabledTag>{}, [&](Entity p_entity, const OrbitCameraComponent&) {
        const bool player = character_is_player(world, p_entity);
        if (best == ENTITY_NULL || (player && !best_player)) {
            best = p_entity;
            best_player = player;
        }
    });
    const bool keep = world.valid(target) && world.has<OrbitCameraComponent>(target) && !world.has<DisabledTag>(target) && (character_is_player(world, target) || !best_player);
    if (!keep && target != best) {
        target = best;
        ready = false;
    }
    if (target == ENTITY_NULL) return;

    const OrbitCameraComponent& cam = *world.try_get<OrbitCameraComponent>(target);
    if (!ready) {
        ready = true;
        const CharacterComponent* body = world.try_get<CharacterComponent>(target);
        const vec3 forward = world.world_transform(target).rotation * vec3(0.0f, 0.0f, 1.0f);
        yaw = character_wrap(character_yaw_of(forward) - (body ? body->model_yaw : 0.0f));
        pitch = std::clamp(START_PITCH, cam.pitch_min, cam.pitch_max);
        pivot = _pivot_target(world);
        distance = cam.distance;
        shoulder = cam.shoulder;
    }
    yaw = character_wrap(yaw - r_session.input.look.x);
    pitch = std::clamp(pitch - r_session.input.look.y, cam.pitch_min, cam.pitch_max);
    r_session.camera.rotation = quatLookAt(_look(yaw, pitch), ORBIT_UP);
}

void OrbitCameraSystem::place(GameSession& r_session, float p_dt)
{
    if (!r_session.world || target == ENTITY_NULL || !ready) return;
    Scene& world = *r_session.world;
    if (!world.valid(target)) return;
    const OrbitCameraComponent* cam = world.try_get<OrbitCameraComponent>(target);
    if (!cam) return;

    const CharacterStateComponent* state = world.try_get<CharacterStateComponent>(target);
    const bool aiming = state && state->character.rotation_mode == RotationMode::AIMING;
    const bool sprinting = state && state->character.gait == Gait::SPRINTING;
    distance = _interp(distance, aiming ? cam->aim_distance : cam->distance, p_dt, ZOOM);
    shoulder = _interp(shoulder, aiming ? cam->aim_shoulder : cam->shoulder, p_dt, ZOOM);
    const vec3 goal = _pivot_target(world);
    pivot.x = _interp(pivot.x, goal.x, p_dt, FOLLOW);
    pivot.z = _interp(pivot.z, goal.z, p_dt, FOLLOW);
    pivot.y = _interp(pivot.y, goal.y, p_dt, FOLLOW_VERTICAL);
    fov = _interp(fov, base_fov + (sprinting ? glm::radians(cam->sprint_fov) : 0.0f), p_dt, FOV_INTERP);
    r_session.camera.fov_y = fov;

    const vec3 look = _look(yaw, pitch);
    vec3 eye = pivot - look * distance + character_right(yaw) * shoulder;
    if (const PhysicsWorld* pw = _physics()) {
        PhysicsQuery query;
        auto body = physics->bodies.find(PhysicsSystem::key(target));
        if (body != physics->bodies.end()) query.ignore = body->second.id;
        const vec3 offset = eye - pivot;
        const float len = length(offset);
        PhysicsHit hit;
        if (len > 1.0e-3f && pw->sphere_cast(pivot, cam->radius, offset / len, len, hit, query)) eye = pivot + offset / len * std::max(hit.distance - WALL_GAP, MIN_DISTANCE);
    }
    r_session.camera.position = eye;
    r_session.camera.rotation = quatLookAt(look, ORBIT_UP);
}

}
