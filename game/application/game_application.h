#pragma once
#include <core/application/application.h>
#include <core/rendering/render_path/game_render_path.h>
#include <game/application/boot_splash.h>
#include <filesystem>
#include <string>

namespace lumen {

struct GameApplication : Application
{
    static constexpr DWORD SPLASH_FINISH_MS = 200;

    std::filesystem::path project_root;
    float title_frame_avg = 0.0f;
    
    bool show_splash = false;
    std::string splash_title;
    BootSplash splash;

    Camera orbit_camera;

    void _spawn_test_model();

    Error _resolve_project_root();
    void _check_splash_close();
    void _open_splash();
    Error on_init() override;
    void on_boot_status(const char* p_status, float p_from, float p_to) override;
    void on_boot_finished() override;
    void on_update(float p_dt) override;
    void on_shutdown() override;

    bool wants_docking() const override { return false; }
    RenderPath* create_render_path() override { return new GameRenderPath(); }

    void update_camera(float p_dt) override;
    const Camera& active_camera() const override { return orbit_camera; }
};

}
