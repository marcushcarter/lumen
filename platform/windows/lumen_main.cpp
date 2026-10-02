#include <windows.h>
#include <core/application/application.h>
#include <cstdio>

#if defined(LUMEN_EDITOR)
    #include <editor/editor_application.h>
    #include <core/io/path.h>
    #include <filesystem>
#elif defined(LUMEN_GAME)
    #include <game/game_application.h>
#else
    #error "lumen_main.cpp requires LUMEN_EDITOR or LUMEN_GAME"
#endif

#if !defined(LUMEN_CONSOLE)
static void attach_console_or_null()
{
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

static std::unique_ptr<lumen::Application> create_application(
    lumen::ApplicationCreateInfo& info, [[maybe_unused]] std::string& ini_storage)
{
#if defined(LUMEN_EDITOR)
    info.window_title = "Lumen Editor";
    ini_storage = (lumen::Paths::roaming_data() / "editor_layout.cfg").string();
    info.ini_path = ini_storage.c_str();
    return std::make_unique<lumen::EditorApplication>();

#elif defined(LUMEN_GAME)
  #if defined(LUMEN_DEV_TOOLS)
    info.window_title = "Lumen Game (Dev Tools)";
  #else
    info.window_title = "Lumen Game";
  #endif
    return std::make_unique<lumen::GameApplication>();
#endif
}

static int run_app()
{
    lumen::ApplicationCreateInfo info;
    info.width  = 1280;
    info.height = 720;

    std::string ini_storage;
    auto app = create_application(info, ini_storage);

    app->initialize(info);
    return app->run();
}

#if defined(LUMEN_CONSOLE)
int main()
{
    return run_app();
}
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    attach_console_or_null();
    return run_app();
}
#endif