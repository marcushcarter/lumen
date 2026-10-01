#pragma once
#include <drivers/windows/window_driver_win32.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <drivers/vulkan/context_driver_vulkan.h>
#include <drivers/imgui/imgui_driver.h>
#include <core/rendering/renderer.h>
#include <core/project/project.h>
#include <core/world/world.h>
#include <core/base/tasks.h>
#include <core/base/error.h>
#include <core/application/frame_stats.h>
#include <string>
#include <filesystem>
#include <functional>

namespace lumen {

struct RenderPath;

struct ApplicationCreateInfo
{
    std::string window_title;
    int width = 1280;
    int height = 720;
    const char* ini_path = nullptr;
};

struct Application
{
    drivers::WindowDriverWin32 win32;

    drivers::ContextDriverVulkan cd;
    drivers::DeviceDriverVulkan dd;
    Renderer renderer;
    RenderPath* render_path = nullptr;
    RenderPath* pending_render_path = nullptr;
    std::function<void()> pending_transition;

    TaskSystem tasks;

    drivers::ImGuiDriver imgui;

    bool paused = false;
    World world;

    Project project;

    FrameStats frame_stats;

    Error initialize(const ApplicationCreateInfo& p_initialize_info);
    void shutdown();
    int run();

    Error project_load(const std::filesystem::path &p_root);
    void project_unload();
    
    void render_path_request(RenderPath* p_next);
    void _apply_pending_render_path();

    virtual Error on_init() = 0;
    virtual void on_update(float p_dt) = 0;
    virtual void on_shutdown() = 0;
    
    virtual bool wants_docking() const { return false; }
    virtual bool wants_custom_titlebar() const { return false; }
    virtual bool should_tick_game() const { return !paused; }
    
    virtual void update_camera(float p_dt) { (void)p_dt; }
    virtual const Camera& active_camera() const { return *world.active_camera; }

    virtual RenderPath* create_render_path() = 0;

    virtual ~Application() = default;
};

}