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
}

int Application::run()
{
    using enum Error;

    auto lastTime = std::chrono::steady_clock::now();

    while (!win32.window_should_close()) {
        auto now = std::chrono::steady_clock::now();
        double delta = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;

        _apply_pending_render_path();

        win32.poll_events();

        cd.surface_set_size(win32.window.width, win32.window.height);
        if (dd.swapchain_update() != Ok) continue;

        renderer.apply_pending_size();

        imgui.begin_frame(renderer.frame_number, renderer.frame_count, renderer.resize_epoch);
        
        update_camera((float)delta);
        renderer.set_camera(active_camera());
        renderer.begin_frame(world);
        
        render_path->build(renderer.graph);
        renderer.compile();

        on_update((float)delta);
        imgui.render();

        Error rec_err = Ok;
        TaskSystem::Handle rec = tasks.dispatch([&]{ rec_err = renderer.record(); }, TaskSystem::Priority::High);
        tasks.wait(rec);
        LUMEN_ERR_FAIL_COND_V(rec_err != Ok, (int)rec_err);
        
        renderer.end_frame();
        imgui.end_frame(renderer.frame_number);
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
    renderer.unload();
    world.unload();
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