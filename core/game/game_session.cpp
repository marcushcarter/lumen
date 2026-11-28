#include <core/game/game_session.h>
#include <core/base/cpu_profiler.h>
#include <core/scene/scene.h>

namespace lumen {

void GameSession::begin(Scene& r_world, const Camera& p_camera)
{
    if (active()) end();
    world = &r_world;
    camera = p_camera;
    time = 0.0;
    frame = 0;
    mode = Mode::PLAYING;
    for (GameSystem* s : systems) s->start(*this);
}

void GameSession::end()
{
    if (!active()) return;
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
    CpuProfiler& cpu = cpu_profiler();
    cpu.zone_begin("Game Update");
    for (GameSystem* s : systems) {
        cpu.zone_begin(s->name);
        s->update(*this, p_dt);
        cpu.zone_end();
    }
    cpu.zone_end();
    world->deferred_flush();
}

}
