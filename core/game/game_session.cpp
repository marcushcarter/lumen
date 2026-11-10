#include <core/game/game_session.h>
#include <core/world/world.h>

namespace lumen {

void GameSession::begin(World& r_world, const Camera& p_camera)
{
    if (active()) end();
    world = &r_world;
    camera = p_camera;
    time = 0.0;
    frame = 0;
    mode = Mode::PLAYING;
    for (GameSystem* s : systems) s->start(*this);
    player.start(*this);
}

void GameSession::end()
{
    if (!active()) return;
    player.stop(*this);
    for (auto it = systems.rbegin(); it != systems.rend(); ++it) (*it)->stop(*this);
    mode = Mode::STOPPED;
    world = nullptr;
}

void GameSession::pause()
{
    if (mode == Mode::PLAYING) mode = Mode::PAUSED;
}

void GameSession::resume()
{
    if (mode == Mode::PAUSED) mode = Mode::PLAYING;
}

void GameSession::update(float p_dt)
{
    if (mode != Mode::PLAYING || !world) return;
    time += p_dt;
    frame++;
    player.pre_update(*this, p_dt);
    for (GameSystem* s : systems) s->update(*this, p_dt);
    player.post_update(*this, p_dt);
    world->deferred_flush();
}

}
