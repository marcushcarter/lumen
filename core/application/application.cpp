#include <core/application/application.h>
#include <core/rendering/render_path/render_path.h>
#include <core/version.h>
#include <windows.h>
#include <chrono>
#include <iostream>

namespace lumen {

Error Application::initialize(const ApplicationCreateInfo& p_create_info)
{
    using enum Error;
    Error err;

    lumen::log_write("%s v%s.stable.official - https://ballisticgames.ca", LUMEN_VERSION_NAME, LUMEN_VERSION_NUMBER);

    cpu_profiler().initialize();

    err = win32.initialize();
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    err = win32.window_create(p_create_info.window_title, p_create_info.width, p_create_info.height, wants_custom_titlebar());
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    win32.window_bind();

    err = cd.full_initialize_windows(win32.window.hwnd);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    err = dd.initialize(cd, cd.optimal_device_index, 3);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    err = renderer.initialize(dd);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    drivers::ImGuiDriverCreateInfo imgui_ci{};
    imgui_ci.hwnd = win32.window.hwnd;
    imgui_ci.instance = cd.instance;
    imgui_ci.physical_device = dd.physical_device;
    imgui_ci.device = dd.device;
    imgui_ci.queue_family = cd.graphics_queue_family;
    imgui_ci.queue = dd.queue_families[cd.graphics_queue_family][0].queue;
    imgui_ci.image_count = renderer.frame_count;
    imgui_ci.render_pass = dd.swapchain.render_pass;
    imgui_ci.sampler = dd.default_sampler.sampler;
    imgui_ci.ini_path = p_create_info.ini_path;
    imgui_ci.enable_docking = wants_docking();
    err = imgui.initialize(imgui_ci);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    render_path = create_render_path();
    render_path->ctx = renderer.make_context();
    render_path->ctx.imgui = &imgui;
    err = render_path->create_resources();
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    tasks.start(std::max(1u, std::thread::hardware_concurrency() - 1u), 1);
    
    err = on_init();
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    frame_stats.initialize(win32.window.hwnd);
    win32.window_show();
    return Ok;
}

void Application::shutdown()
{
    tasks.stop();

    dd.device_wait_idle();

    project_unload();
    
    if (render_path) {
        render_path->destroy_resources();
        delete render_path;
        render_path = nullptr;
    }

    imgui.shutdown();
    renderer.shutdown();
    dd.shutdown();
    cd.shutdown();
    
    win32.window_free();
    win32.shutdown();

    cpu_profiler().shutdown();
}

int Application::run()
{
    using enum Error;

    CpuProfiler& cpu = cpu_profiler();
    auto lastTime = std::chrono::steady_clock::now();

    while (!win32.window_should_close()) {
        if (win32.window_is_minimized()) {
            WaitMessage();
            win32.poll_events();
            continue;
        }
        
        cpu.begin_frame(renderer.frame_number);
        frame_stats.update(cpu);

        cpu.zone_begin("Swapchain");
        _apply_pending_render_path();
        cd.surface_set_size(win32.window.width, win32.window.height);
        if (dd.swapchain_update() != Ok) {
            win32.poll_events();
            cpu.zone_end();
            continue;
        }
        renderer.apply_pending_size();
        cpu.zone_end();

        cpu.zone_begin("Wait GPU + Acquire", CpuProfiler::FLAG_WAIT);
        renderer.acquire_frame();
        cpu.zone_end();

        auto now = std::chrono::steady_clock::now();
        double delta = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;

        cpu.zone_begin("Poll Events");
        win32.poll_events();
        cpu.zone_end();

        cpu.zone_begin("ImGui Begin");
        imgui.begin_frame(renderer.frame_number, renderer.frame_count, renderer.resize_epoch);
        cpu.zone_end();

        cpu.zone_begin("Camera");
        update_camera((float)delta);
        renderer.set_camera(active_camera());
        cpu.zone_end();

        cpu.zone_begin("Frame Build");
        renderer.begin_frame(world);
        cpu.zone_end();

        cpu.zone_begin("Graph Build");
        render_path->build(renderer.graph);
        renderer.compile();
        cpu.zone_end();

        cpu.zone_begin("Update");
        on_update((float)delta);
        cpu.zone_end();

        cpu.zone_begin("ImGui Render");
        imgui.render();
        cpu.zone_end();

        cpu.zone_begin("Record");
        Error rec_err = renderer.record();
        LUMEN_ERR_FAIL_COND_V(rec_err != Ok, (int)rec_err);
        cpu.zone_end();

        cpu.zone_begin("Submit + Present");
        renderer.end_frame();
        cpu.zone_end();

        cpu.zone_begin("End Frame");
        imgui.end_frame(renderer.frame_number);
        cpu.zone_end();
    }

    on_shutdown();
    shutdown();
    return 0;
}

Error Application::project_load(const std::filesystem::path &p_root)
{
    using enum Error;
    Error err = project.load(p_root);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    err = renderer.load(project.content_dir);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    err = world.load();
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    return Ok;
}

void Application::project_unload()
{
    dd.device_wait_idle();
    world.unload();
    renderer.unload();
    project.unload();
}

void Application::render_path_request(RenderPath* p_next)
{
    if (pending_render_path) delete pending_render_path;
    pending_render_path = p_next;
}

void Application::_apply_pending_render_path()
{
    using enum Error;
    if (!pending_render_path) return;

    dd.device_wait_idle();

    if (pending_transition) { pending_transition(); pending_transition = nullptr; }
    
    renderer.resize_epoch++;

    if (render_path) {
        render_path->destroy_resources();
        delete render_path;
    }

    render_path = pending_render_path;
    pending_render_path = nullptr;
    render_path->ctx = renderer.make_context();
    render_path->ctx.imgui = &imgui;
    if (render_path->create_resources() != Ok)
        log_write("Application: render path create_resources failed.");
}

}
