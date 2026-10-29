#pragma once
#include <core/application/application.h>
#include <core/rendering/render_path/game_render_path.h>
#include <filesystem>

namespace lumen {

struct GameApplication : Application
{
    /*******************/
    /**** LIFECYCLE ****/
    /*******************/

    std::filesystem::path project_root;
    float title_frame_avg = 0.0f;

    void _spawn_test_model();
    Error on_init() override;
    void on_update(float p_dt) override;
    void on_shutdown() override;

    bool wants_docking() const override { return false; }
    RenderPath* create_render_path() override { return new GameRenderPath(); }

    /****************/
    /**** CAMERA ****/
    /****************/

    Camera orbit_camera;

    void update_camera(float p_dt) override;
    const Camera& active_camera() const override { return orbit_camera; }
};

}
