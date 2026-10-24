#pragma once
#include <functional>
#include <filesystem>

namespace lumen {

namespace drivers { struct WindowDriverWin32; }
namespace drivers { struct ImGuiDriver; }
struct Renderer;
struct Project;
struct World;
struct ProjectManager;
struct WorldTab;
struct Drawer;
struct TabManager;
struct EditorSelection;
struct EditorRenderPath;
struct ProfilingSettings;
struct EditorSettings;
struct EditorResources;
struct AssetImportTracker;
struct AssetRegistry;
struct EditorFolders;
struct TextureViewer;
struct ThumbnailCache;

struct EditorContext
{
    drivers::WindowDriverWin32* win32 = nullptr;
    drivers::ImGuiDriver* imgui = nullptr;

    Renderer* renderer = nullptr;
    Project* project = nullptr;
    World* world = nullptr;
    
    ProjectManager* project_manager = nullptr;
    WorldTab* world_tab = nullptr;
    Drawer* drawer = nullptr;
    TabManager* tabs = nullptr;
    EditorSelection* selection = nullptr;
    
    EditorRenderPath* render_path = nullptr;
    EditorSettings* settings = nullptr;
    ProfilingSettings* profiling = nullptr;
    EditorResources* resources = nullptr;
    AssetImportTracker* imports = nullptr;
    AssetRegistry* assets = nullptr;
    EditorFolders* folders = nullptr;
    TextureViewer* texture_viewer = nullptr;
    ThumbnailCache* thumbnails = nullptr;
        
    std::function<void(const std::filesystem::path&)> open_project_callback;
    std::function<void()> close_project_callback;

    std::function<bool(const std::filesystem::path&)> run_is_running;
    std::function<void(const std::filesystem::path&)> run_toggle;

    std::function<bool()> pie_is_playing;
    std::function<void()> pie_toggle_play;
    std::function<bool()> pie_is_paused;
    std::function<void()> pie_toggle_pause;
    std::function<void()> pie_step_frame;
    std::function<void()> pie_toggle_eject;
};

}