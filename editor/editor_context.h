#pragma once
#include <functional>
#include <filesystem>

namespace lumen {

namespace drivers { struct WindowDriverWin32; }
namespace drivers { struct ImGuiDriver; }
struct Renderer;
struct TaskSystem;
struct Project;
struct World;
struct ProjectManager;
struct Editor;
struct Entity;
struct EditorRenderPath;
struct ProfilingSettings;
struct EditorSettings;
struct EditorResources;
struct AssetImportTracker;

struct EditorContext
{
    drivers::WindowDriverWin32* win32 = nullptr;
    drivers::ImGuiDriver* imgui = nullptr;

    Renderer* renderer = nullptr;
    TaskSystem* tasks = nullptr;
    Project* project = nullptr;
    World* world = nullptr;
    
    ProjectManager* project_manager = nullptr;
    Editor* editor = nullptr;
    Entity* selected = nullptr;
    
    EditorRenderPath* render_path = nullptr;
    EditorSettings* settings = nullptr;
    ProfilingSettings* profiling = nullptr;
    EditorResources* resources = nullptr;
    AssetImportTracker* imports = nullptr;
        
    std::function<void(const std::filesystem::path&)> open_project_callback;
    std::function<void()> close_project_callback;
    std::function<bool()> confirm_gpu_support;

    std::function<bool()> pie_is_playing;
    std::function<void()> pie_toggle_play;
    std::function<bool()> pie_is_paused;
    std::function<void()> pie_toggle_pause;
    std::function<void()> pie_step_frame;
    std::function<void()> pie_toggle_eject;
};

}
