#include <core/game/player.h>
#include <core/game/game_session.h>
#include <core/input/input.h>
#include <core/world/components.h>

namespace lumen {

Entity Player::resolve(World& r_world, Guid p_controlled)
{
    if (p_controlled.valid()) {
        const Entity e = r_world.find(p_controlled);
        if (r_world.valid(e) && r_world.has<CharacterComponent>(e)) return e;
    }
    Entity found = ENTITY_NULL;
    r_world.view<CharacterComponent>([&](Entity p_entity, const CharacterComponent&) {
        if (found == ENTITY_NULL) found = p_entity;
    });
    return found;
}

void Player::start(GameSession& r_session)
{
    (void)r_session;
    controlled = ENTITY_NULL;
    camera.reset();
}

void Player::stop(GameSession& r_session)
{
    (void)r_session;
    controlled = ENTITY_NULL;
}

void Player::pre_update(GameSession& r_session, float p_dt)
{
    (void)p_dt;
    if (!r_session.world) return;
    World& world = *r_session.world;
    const Entity next = resolve(world, world.player.controlled);
    if (next != controlled) {
        if (world.valid(controlled)) {
            if (CharacterStateComponent* old = world.try_get<CharacterStateComponent>(controlled)) {
                old->move = vec2(0.0f);
                old->run = old->walk = old->jump = false;
            }
        }
        controlled = next;
    }
    if (!world.valid(controlled)) return;
    if (!world.has<CharacterStateComponent>(controlled)) world.add<CharacterStateComponent>(controlled);
    CharacterStateComponent& state = *world.try_get<CharacterStateComponent>(controlled);
    const Input* input = r_session.input;
    state.move = input ? input->axis(InputAction::MOVE) : vec2(0.0f);
    state.run = input && input->down(InputAction::RUN);
    state.walk = input && input->down(InputAction::WALK);
    state.jump = state.jump || (input && input->pressed(InputAction::JUMP));
    state.move_yaw = camera.initialized ? camera.yaw : state.facing;
    state.has_move_yaw = true;
}

void Player::post_update(GameSession& r_session, float p_dt)
{
    if (!r_session.world || !r_session.world->valid(controlled)) return;
    World& world = *r_session.world;
    if (const CharacterStateComponent* state = world.try_get<CharacterStateComponent>(controlled)) {
        if (state->sprinting) camera.request(CameraState::SPRINT);
    }
    camera.update(world, controlled, world.player.camera, r_session.input, r_session.physics, p_dt, r_session.camera);
}

}
