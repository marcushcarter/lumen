
#pragma once
#include <core/application/application.h>
#include <core/rendering/render_path/game_render_path.h>
#include <filesystem>

namespace lumen {

struct GameApplication : Application
{
    std::filesystem::path project_root;
    float title_frame_avg = 0.0f;

    Camera orbit_camera;
    float orbit_time = 0.0f;
    
    Error on_init() override;
    void on_update(float p_dt) override;
    void on_shutdown() override;

    void update_camera(float p_dt) override;
    const Camera& active_camera() const override { return orbit_camera; }

    bool wants_docking() const override { return false; }
    BackgroundPolicy background_policy() const override { return { .tick_ms = 1000, .throttle = true, .pause_sim = true }; }
    RenderPath* create_render_path() override { return new GameRenderPath(); }
};

}