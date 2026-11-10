#pragma once
#include <core/game/player_camera.h>
#include <core/world/world.h>

namespace lumen {

struct GameSession;

struct Player
{
    Entity controlled = ENTITY_NULL;
    PlayerCamera camera;

    void start(GameSession& r_session);
    void stop(GameSession& r_session);
    void pre_update(GameSession& r_session, float p_dt);
    void post_update(GameSession& r_session, float p_dt);

    static Entity resolve(World& r_world, Guid p_controlled);
};

}
