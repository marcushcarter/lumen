#pragma once
#include <functional>
#include <filesystem>

namespace lumen {

namespace drivers { struct WindowDriverWin32; }
namespace drivers { struct ImGuiDriver; }
struct Renderer;
struct Project;
struct TaskSystem;
struct EditorRenderPath;
struct EditorSettings;
struct EditorResources;
struct AssetImportTracker;
struct ProjectManager;
struct Editor;
struct ProfilingSettings;

struct EditorContext
{
    drivers::WindowDriverWin32* win32 = nullptr;
    drivers::ImGuiDriver* imgui = nullptr;
    Renderer* renderer = nullptr;
    EditorRenderPath* render_path = nullptr;
    Project* project = nullptr;
    TaskSystem* tasks = nullptr;
    ProfilingSettings* profiling = nullptr;
    
    EditorSettings* settings = nullptr;
    EditorResources* resources = nullptr;
    AssetImportTracker* imports = nullptr;
    
    ProjectManager* project_manager = nullptr;
    Editor* editor = nullptr;
        
    std::function<void(const std::filesystem::path&)> open_project_callback;
    std::function<void()> close_project_callback;
    std::function<bool()> confirm_gpu_support;

    std::function<bool()> pie_is_playing;
    std::function<void()> pie_toggle_play;
    std::function<bool()> pie_is_paused;
    std::function<void()> pie_toggle_pause;
};

}
