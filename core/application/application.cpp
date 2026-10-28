#include <core/application/application.h>
#include <core/rendering/render_path/render_path.h>
#include <core/io/path.h>
#include <core/version.h>
#include <imgui.h>
#include <windows.h>
#include <cfloat>
#include <chrono>
#include <fstream>

namespace lumen {

Error Application::_initialize(const ApplicationCreateInfo& p_create_info)
{
    using enum Error;

    Error err = win32.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = win32.window_create(p_create_info.window_title, p_create_info.width, p_create_info.height, wants_custom_titlebar());
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    win32.window_bind();
    win32.window_set_minimum_size(900, 600);

    err = cd.full_initialize_windows(win32.window.hwnd);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = dd.initialize(cd, cd.optimal_device_index, 3);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = renderer.initialize(dd, profiling);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = world.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

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
    imgui_ci.enable_docking = wants_docking();
    err = imgui.initialize(imgui_ci);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    render_path = create_render_path();
    render_path->ctx = renderer.make_context();
    render_path->ctx.imgui = &imgui;
    err = render_path->create_resources();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = on_init();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    frame_stats.initialize(win32.window.hwnd);
    err = frame_limiter.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    win32.window_show();
    return OK;
}

Error Application::initialize(const ApplicationCreateInfo& p_create_info)
{
    using enum Error;

    log_write("\n\n%s v%s.stable.official - https://ballisticgames.ca", LUMEN_VERSION_NAME, LUMEN_VERSION_NUMBER);

    cpu_profiler().initialize();

    Error err = job_system().initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = _initialize(p_create_info);
    if (err != OK) job_system().shutdown();
    return err;
}

void Application::shutdown()
{
    job_system().shutdown();

    dd.device_wait_idle();

    project_unload();
    world.shutdown();

    if (render_path) {
        render_path->destroy_resources();
        delete render_path;
        render_path = nullptr;
    }
    delete pending_render_path;
    pending_render_path = nullptr;
    pending_transition = nullptr;

    imgui.shutdown();
    renderer.shutdown();
    dd.shutdown();
    cd.shutdown();

    win32.window_free();
    win32.shutdown();

    frame_limiter.shutdown();
    cpu_profiler().shutdown();
}

Error Application::_frame()
{
    using enum Error;

    CpuProfiler& cpu = cpu_profiler();
    cpu.begin_frame(renderer.frame_number);
    frame_stats.update(cpu);

    cpu.zone_begin("Swapchain");
    Error err = _apply_pending_render_path();
    if (err != OK) {
        cpu.zone_end();
        return err;
    }
    cd.surface_set_vsync_mode(vsync_mode());
    cd.surface_set_size(win32.window.width, win32.window.height);
    const bool swapchain_ok = dd.swapchain_update() == OK;
    if (swapchain_ok) renderer.apply_pending_size();
    cpu.zone_end();
    if (!swapchain_ok) return OK;

    cpu.zone_begin("Wait GPU + Acquire");
    err = renderer.acquire_frame();
    cpu.zone_end();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    if (!renderer.frame_acquired) return OK;

    auto now = std::chrono::steady_clock::now();
    double delta = std::chrono::duration<double>(now - last_time).count();
    last_time = now;
    if (delta > 0.25) delta = 0.25;

    if (!win32.window_is_focused()) ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    cpu.zone_begin("ImGui Begin");
    imgui.begin_frame(renderer.frame_number, renderer.frame_count, renderer.resize_epoch);
    cpu.zone_end();

    cpu.zone_begin("Camera");
    update_camera((float)delta);
    renderer.set_camera(active_camera());
    cpu.zone_end();

    cpu.zone_begin("Frame Build");
    err = renderer.begin_frame(render_world());
    cpu.zone_end();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    cpu.zone_begin("Graph Build");
    render_path->build(renderer.main_view.graph);
    err = renderer.compile();
    cpu.zone_end();
    LUMEN_ERR_FAIL_COND_V_MSG(err != OK, err, "Render graph compile failed (transient allocation).");

    cpu.zone_begin("Update");
    on_update((float)delta);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F12, ImGuiInputFlags_RouteGlobal)) screenshot_request();
    world.deferred_flush();
    cpu.zone_end();

    cpu.zone_begin("ImGui Render");
    imgui.render();
    cpu.zone_end();

    cpu.zone_begin("Record");
    err = renderer.record();
    cpu.zone_end();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    cpu.zone_begin("Submit + Present");
    err = renderer.end_frame();
    cpu.zone_end();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    cpu.zone_begin("End Frame");
    imgui.end_frame(renderer.frame_number);
    cpu.zone_end();

    return OK;
}

void Application::_update_window_state()
{
    const bool focused = win32.window_is_focused();
    const bool visible = win32.window_is_visible();
    const bool focus_gained = win32.window_consume_focus_gained();
    const bool regained = (focused && !window_focused) || (visible && !window_visible) || (focus_gained && focused);

    if (regained) last_time = std::chrono::steady_clock::now();
    if (focused != window_focused) {
        window_focused = focused;
        on_focus_changed(focused);
    }
    if (visible != window_visible) {
        window_visible = visible;
        on_visibility_changed(visible);
    }
}

Error Application::run()
{
    using enum Error;

    CpuProfiler& cpu = cpu_profiler();
    last_time = std::chrono::steady_clock::now();
    background_last_tick = last_time;
    window_focused = win32.window_is_focused();
    window_visible = win32.window_is_visible();

    Error err = OK;
    while (!win32.window_should_close()) {
        const BackgroundPolicy policy = background_policy();

        if (_should_render(policy)) {
            const float cap = fps_cap();
            if (cap > 0.0f) {
                cpu.zone_begin("Frame Limiter", CpuProfiler::FLAG_WAIT);
                frame_limiter.wait(cap);
                cpu.zone_end();
            }
            win32.poll_events();
        } else {
            const auto elapsed = std::chrono::steady_clock::now() - background_last_tick;
            const auto remaining = std::chrono::milliseconds(policy.housekeeping_ms) - std::chrono::ceil<std::chrono::milliseconds>(elapsed);
            cpu.zone_begin("Background Wait", CpuProfiler::FLAG_WAIT);
            win32.wait_events(remaining.count() > 0 ? (uint32_t)remaining.count() : 0u);
            cpu.zone_end();
        }
        if (win32.window_should_close()) break;

        _update_window_state();
        if (!_should_render(policy)) {
            const auto now = std::chrono::steady_clock::now();
            if (now - background_last_tick >= std::chrono::milliseconds(policy.housekeeping_ms)) {
                background_last_tick = now;
                on_background_tick();
            }
            continue;
        }
        win32.window_consume_resized();

        err = _frame();
        if (err != OK) break;
    }

    dd.device_wait_idle();
    on_shutdown();
    shutdown();
    return err;
}

void Application::report_fatal_error(Error p_error)
{
    log_write("[Lumen] Fatal error: %s", error_names[static_cast<size_t>(p_error)]);

    const std::filesystem::path dir = Paths::logs();
    if (dir.empty()) return;

    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[64];
    swprintf_s(name, L"crash_%04hu-%02hu-%02hu_%02hu-%02hu-%02hu.txt", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    const std::filesystem::path file = dir / name;

    const std::string text = log_sink().to_string();
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();

    Paths::reveal_in_explorer(file);
}

void Application::screenshot_request()
{
    if (!render_path) return;
    render_path->screenshot.reveal = screenshot_reveal;
    render_path->screenshot.requested = true;
}

Error Application::project_load(const std::filesystem::path &p_root)
{
    using enum Error;

    Error err = project.load(p_root);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = renderer.load(project.content_dir, project.assets_dir);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = world.load();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    log_write("Project loaded: %s (%s)", project.name.c_str(), p_root.string().c_str());

    return OK;
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
    delete pending_render_path;
    pending_render_path = p_next;
}

Error Application::_apply_pending_render_path()
{
    using enum Error;
    if (!pending_render_path) return OK;

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
    Error err = render_path->create_resources();
    LUMEN_ERR_FAIL_COND_V_MSG(err != OK, err, "Application: render path create_resources failed.");
    dd.pipeline_cache_save();
    return OK;
}

}
