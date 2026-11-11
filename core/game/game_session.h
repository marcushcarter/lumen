#pragma once
#include <core/world/camera.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct World;
struct GameSession;

struct GameSystem
{
    virtual ~GameSystem() = default;
    virtual void start(GameSession& r_session) { (void)r_session; }
    virtual void update(GameSession& r_session, float p_dt) = 0;
    virtual void stop(GameSession& r_session) { (void)r_session; }
};

struct GameSession
{
    enum class Mode : uint8_t { STOPPED, PLAYING, PAUSED };

    World* world = nullptr;
    Camera camera;
    Mode mode = Mode::STOPPED;
    double time = 0.0;
    uint64_t frame = 0;
    std::vector<GameSystem*> systems;

    bool active() const { return mode != Mode::STOPPED; }
    bool playing() const { return mode == Mode::PLAYING; }
    bool paused() const { return mode == Mode::PAUSED; }

    void begin(World& r_world, const Camera& p_camera);
    void end();
    void pause();
    void resume();
    void update(float p_dt);
};

}
