#include <game/application/game_application.h>
#include <core/io/path.h>
#include <imgui.h>
#include <windows.h>
#include <shellapi.h>
#include <filesystem>

namespace lumen {

Error GameApplication::on_init()
{
    using enum Error;

    screenshot_reveal = false;
    if (project_root.empty()) project_root = "D:/TestLumen";
    Error err = project_load(project_root);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    win32.window_set_title(project.window_title());
    win32.window_set_size(project.settings.width, project.settings.height);

    std::error_code ec;
    const std::filesystem::path icon = project.window_icon_path();
    if (project.settings.window_icon.valid() && std::filesystem::exists(icon, ec) && win32.window_set_icon_file(icon.wstring()) != OK) {
        log_write("Game: couldn't load the window icon '%s'.", icon.string().c_str());
    }

    return OK;
}

void GameApplication::on_update(float p_dt)
{
    (void)p_dt;
    renderer.request_size(win32.window.width, win32.window.height);

    // if (frame_stats.frame_avg > 0.0f && frame_stats.frame_avg != title_frame_avg) {
    //     title_frame_avg = frame_stats.frame_avg;
    //     char title[256];
    //     std::snprintf(title, sizeof(title), "%s - %.2f ms (%.0f fps)", project.window_title().c_str(), title_frame_avg, 1000.0f / title_frame_avg);
    //     win32.window_set_title(title);
    // }

    world.atmosphere.time_of_day += p_dt / 5.0f;
    if (world.atmosphere.time_of_day > 24.0f) world.atmosphere.time_of_day = 0.0f;
}

void GameApplication::update_camera(float )
{
    vec3 target = vec3(0.0f, 2.0f, 0.0f);
    float radius = -1.5f;
    float height = -0.5f;
    float angle = 0.0f;

    const vec3 eye = target + vec3(radius * std::cos(angle), height, -radius * std::sin(angle));
    orbit_camera.position = eye;
    orbit_camera.rotation = quatLookAt(normalize(target - eye), vec3(0.0f, 1.0f, 0.0f));
}

void GameApplication::on_shutdown() {}

}
