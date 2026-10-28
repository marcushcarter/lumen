#include <windows.h>
#include <core/application/application.h>
#include <core/application/launch_args.h>
#include <game/game_application.h>
#include <cstdio>

#if defined(LUMEN_EDITOR)
    #include <editor/editor_application.h>
#elif !defined(LUMEN_GAME)
    #error "lumen_main.cpp requires LUMEN_EDITOR or LUMEN_GAME"
#endif

#if !defined(LUMEN_CONSOLE)
static void attach_console_or_null() {
    FILE* dummy;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
        freopen_s(&dummy, "CONIN$",  "r", stdin);
    } else {
        freopen_s(&dummy, "NUL", "w", stdout);
        freopen_s(&dummy, "NUL", "w", stderr);
        freopen_s(&dummy, "NUL", "r", stdin);
    }
}
#endif

static std::unique_ptr<lumen::Application> create_application(const lumen::LaunchArgs& p_args, lumen::ApplicationCreateInfo& info) {
#if defined(LUMEN_EDITOR)
    if (!p_args.game) {
        info.window_title = "Lumen Editor";
        return std::make_unique<lumen::EditorApplication>();
    }
#endif

#if defined(LUMEN_DEV_TOOLS)
    info.window_title = p_args.pie ? "Lumen Game (PIE)" : "Lumen Game (Dev Tools)";
#else
    info.window_title = "Lumen Game";
#endif

    auto game = std::make_unique<lumen::GameApplication>();
    game->project_root = p_args.project;
    return game;
}

static int run_app() {
    lumen::ApplicationCreateInfo info;
    info.width  = 1280;
    info.height = 720;

    const lumen::LaunchArgs args = lumen::LaunchArgs::parse_command_line();

    auto app = create_application(args, info);
    
    lumen::Error err = app->initialize(info);
    if (err == lumen::Error::OK) err = app->run();
    if (err != lumen::Error::OK) app->report_fatal_error(err);
    return err == lumen::Error::OK ? 0 : 1;
}

#if defined(LUMEN_CONSOLE)
int main() {
    return run_app();
}
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    attach_console_or_null();
    return run_app();
}
#endif