#include <game/application/game_application.h>
#include <core/io/path.h>
#include <core/io/image_io.h>
#include <core/project/project.h>
#include <core/world/components.h>
#include <core/world/world_file.h>
#include <imgui.h>
#include <windows.h>
#include <shellapi.h>
#include <filesystem>

namespace lumen {

void GameApplication::_load_world()
{
    if (world_path.empty()) {
        log_write("Game: no world to load, starting empty.");
        return;
    }
    WorldFile file;
    if (WorldFile::read(world_path, world, false, file) != Error::OK) log_write("Game: failed to load the world '%s'.", world_path.string().c_str());
}

Error GameApplication::_resolve_project_root()
{
    using enum Error;
    if (!project_root.empty()) return OK;
    wchar_t exe[MAX_PATH]{};
    const DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    LUMEN_ERR_FAIL_COND_V_MSG(n == 0 || n == MAX_PATH, FAILED, "Game: GetModuleFileName failed.");
    project_root = std::filesystem::path(exe).parent_path();
    return OK;
}

Error GameApplication::on_init()
{
    using enum Error;

    screenshot_reveal = false;
    Error err = _resolve_project_root();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = project_load(project_root);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    _load_world();
    update_camera(0.0f);
    session.begin(world, orbit_camera);

    win32.window_set_title(main_window, project.window_title());
    win32.window_set_size(main_window, project.settings.width, project.settings.height);

    std::error_code ec;
    const std::filesystem::path icon = project.icon_path();
    if (project.settings.window_icon.valid() && std::filesystem::exists(icon, ec) && win32.window_set_icon_file(main_window, icon.wstring()) != OK) {
        log_write("Game: couldn't load the window icon '%s'.", icon.string().c_str());
    }

    return OK;
}

void GameApplication::_open_splash()
{
    if (!show_splash) return;
    show_splash = false;
    if (_resolve_project_root() != Error::OK) return;
    ImageData<uint8_t, 4> image = ImageIO::load_from_file<uint8_t, 4>((project_root / Project::FILE_SPLASH).wstring());
    if (!image.valid()) return;

    std::vector<uint32_t> bgra((size_t)image.width * image.height);
    for (size_t i = 0; i < bgra.size(); i++) {
        const uint8_t* s = image.pixels + i * 4;
        bgra[i] = s[2] | (s[1] << 8) | (s[0] << 16) | ((uint32_t)s[3] << 24);
    }
    const int w = image.width;
    const int h = image.height;
    ImageIO::free_image(image);
    show_splash = splash.show(win32, splash_title, bgra.data(), w, h) == Error::OK;
}

void GameApplication::on_boot_status(const char* p_status, float p_from, float p_to)
{
    if (show_splash && !splash.visible()) _open_splash();
    if (!splash.visible()) return;
    _check_splash_close();
    splash.set_status(p_status);
    splash.set_progress(p_from, p_to);
}

void GameApplication::on_boot_finished()
{
    if (splash.visible()) {
        _check_splash_close();
        splash.set_progress(1.0f, 1.0f);
        Sleep(SPLASH_FINISH_MS);
    }
    splash.close();
    show_splash = false;
}

void GameApplication::_check_splash_close()
{
    if (!splash.close_requested()) return;
    splash.close();
    ExitProcess(0);
}

void GameApplication::on_update(float p_dt)
{
    (void)p_dt;
    renderer.request_size(main_window.width, main_window.height);

    if (frame_stats.frame_avg > 0.0f && frame_stats.frame_avg != title_frame_avg) {
        title_frame_avg = frame_stats.frame_avg;
        char title[256];
        std::snprintf(title, sizeof(title), "%s - %.2f ms (%.0f fps)", project.window_title().c_str(), title_frame_avg, 1000.0f / title_frame_avg);
        win32.window_set_title(main_window, title);
    }
}

void GameApplication::update_camera(float )
{    
    vec3 target = vec3(0.0f, 1.0f, 0.0f);
    float radius = 3.0f;
    float height = 1.0f;
    float angle = 0.0f;

    const vec3 eye = target + vec3(radius * std::cos(angle), height, -radius * std::sin(angle));
    orbit_camera.position = eye;
    orbit_camera.rotation = quatLookAt(normalize(target - eye), vec3(0.0f, 1.0f, 0.0f));
}

void GameApplication::on_shutdown()
{
    session.end();
    splash.close();
}

}
