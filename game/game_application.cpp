#include <game/game_application.h>
#include <core/io/path.h>
#include <imgui.h>
#include <windows.h>
#include <shellapi.h>

namespace lumen {

Error GameApplication::on_init()
{
    using enum Error;

    // Error err = project_load(Paths::executable_dir());
    Error err = project_load("D:/TestLumen");
    if (err == CANCELED) return err;
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    
    win32.window_set_title(project.name);
    win32.window_set_size(project.settings.width, project.settings.height);
    
    return OK;
}

void GameApplication::on_update(float p_dt)
{
    (void)p_dt;
    renderer.request_size(win32.window.width, win32.window.height);

    if (frame_stats.frame_avg > 0.0f && frame_stats.frame_avg != title_frame_avg) {
        title_frame_avg = frame_stats.frame_avg;
        char title[256];
        std::snprintf(title, sizeof(title), "%s - %.2f ms (%.0f fps)", project.name.c_str(), title_frame_avg, 1000.0f / title_frame_avg);
        win32.window_set_title(title);
    }
}

void GameApplication::on_shutdown() {}

}