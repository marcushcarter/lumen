#pragma once
#include <functional>
#include <filesystem>
#include <vector>
#include <drivers/windows/window_driver_win32.h>

namespace lumen {

namespace drivers { struct ImGuiDriver; }
namespace drivers { struct GitDriver; }
struct Renderer;
struct Project;
struct World;
struct EditorPlay;
struct ProjectManager;
struct WorldTab;
struct SharedPanels;
struct TabManager;
struct EditorSelection;
struct EditorRenderPath;
struct ProfilingSettings;
struct EditorSettings;
struct EditorResources;
struct AssetImportTracker;
struct AssetRegistry;
struct TextureViewer;
struct ImportDialog;
struct ReferencesDialog;
struct ThumbnailCache;

struct EditorContext
{
    drivers::WindowDriverWin32* win32 = nullptr;
    drivers::WindowDriverWin32::Window* main_window = nullptr;
    drivers::ImGuiDriver* imgui = nullptr;
    drivers::GitDriver* git = nullptr;

    Renderer* renderer = nullptr;
    Project* project = nullptr;
    World* world = nullptr;

    ProjectManager* project_manager = nullptr;
    WorldTab* world_tab = nullptr;
    SharedPanels* shared_panels = nullptr;
    TabManager* tabs = nullptr;
    EditorSelection* selection = nullptr;

    EditorRenderPath* render_path = nullptr;
    EditorSettings* settings = nullptr;
    ProfilingSettings* profiling = nullptr;
    EditorResources* resources = nullptr;
    AssetImportTracker* imports = nullptr;
    AssetRegistry* assets = nullptr;
    TextureViewer* texture_viewer = nullptr;
    ImportDialog* import_dialog = nullptr;
    ReferencesDialog* references_dialog = nullptr;
    ThumbnailCache* thumbnails = nullptr;

    std::function<void(const std::filesystem::path&)> open_project_callback;
    std::function<void()> close_project_callback;
    std::function<void()> save_all_callback;
    std::function<void(const std::vector<std::filesystem::path>&)> reload_asset_tabs;
    std::function<void(std::function<void()>)> reload_project_callback;

    std::function<bool(const std::filesystem::path&)> run_is_running;
    std::function<void(const std::filesystem::path&)> run_toggle;

    EditorPlay* play = nullptr;
    std::function<bool()> pie_is_playing;
    std::function<void()> pie_toggle_play;
    std::function<bool()> pie_is_paused;
    std::function<void()> pie_toggle_pause;
    std::function<void()> pie_step_frame;
    std::function<void()> pie_toggle_eject;
};

}
