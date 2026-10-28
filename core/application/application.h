#pragma once
#include <drivers/windows/window_driver_win32.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <drivers/vulkan/context_driver_vulkan.h>
#include <drivers/imgui/imgui_driver.h>
#include <core/rendering/renderer.h>
#include <core/project/project.h>
#include <core/world/world.h>
#include <core/base/jobs.h>
#include <core/base/error.h>
#include <core/base/profiling.h>
#include <core/application/frame_stats.h>
#include <core/application/frame_limiter.h>
#include <string>
#include <filesystem>
#include <functional>
#include <chrono>

namespace lumen {

struct RenderPath;

struct ApplicationCreateInfo
{
    std::string window_title;
    int width = 1280;
    int height = 720;
};

struct BackgroundPolicy
{
    bool render_unfocused = true;
    uint32_t housekeeping_ms = 1000;
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

    drivers::ImGuiDriver imgui;

    ProfilingSettings profiling;

    bool paused = false;
    bool screenshot_reveal = true;
    World world;

    Project project;

    FrameStats frame_stats;
    FrameLimiter frame_limiter;
    std::chrono::steady_clock::time_point last_time;

    bool window_focused = true;
    bool window_visible = true;
    std::chrono::steady_clock::time_point background_last_tick;

    Error initialize(const ApplicationCreateInfo& p_initialize_info);
    Error _initialize(const ApplicationCreateInfo& p_initialize_info);
    void shutdown();

    Error _frame();
    Error run();
    void _update_window_state();
    bool _should_render(const BackgroundPolicy& p_policy) const { return window_visible && (window_focused || p_policy.render_unfocused); }
    void report_fatal_error(Error p_error);
    void screenshot_request();

    Error project_load(const std::filesystem::path &p_root);
    void project_unload();
    
    void render_path_request(RenderPath* p_next);
    Error _apply_pending_render_path();

    virtual Error on_init() = 0;
    virtual void on_update(float p_dt) = 0;
    virtual void on_shutdown() = 0;
    
    virtual bool wants_docking() const { return false; }
    virtual bool wants_custom_titlebar() const { return false; }
    virtual bool should_tick_game() const { return !paused; }
    virtual VsyncMode vsync_mode() const { return VsyncMode::OFF; }
    virtual float fps_cap() const { return 0.0f; }
    virtual BackgroundPolicy background_policy() const { return {}; }

    virtual void on_focus_changed(bool p_focused) { (void)p_focused; }
    virtual void on_visibility_changed(bool p_visible) { (void)p_visible; }
    virtual void on_background_tick() {}
    
    virtual void update_camera(float p_dt) { (void)p_dt; }
    virtual const Camera& active_camera() const { return *world.active_camera; }
    virtual const World& render_world() const { return world; }

    virtual RenderPath* create_render_path() = 0;

    virtual ~Application() = default;
};

}