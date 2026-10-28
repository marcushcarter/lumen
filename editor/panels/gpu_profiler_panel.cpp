#include <editor/panels/gpu_profiler_panel.h>
#include <drivers/imgui/ui_style.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/rendering/features/editor/debug_view.h>
#include <core/base/cpu_profiler.h>
#include <core/base/profiling.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <vulkan/vk_enum_string_helper.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace lumen {

static void _append(std::string& r_out, const char* p_fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, p_fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), p_fmt, args);
    va_end(args);
    if (n > 0) r_out.append(buf, (size_t)std::min(n, (int)sizeof(buf) - 1));
}

static const char* _kind_name(GpuProfiler::MarkKind p_kind)
{
    switch (p_kind) {
        case GpuProfiler::MarkKind::PASS: return "PASS";
        case GpuProfiler::MarkKind::DRAW: return "DRAW";
        case GpuProfiler::MarkKind::BARRIER: return "SYNC";
        case GpuProfiler::MarkKind::DISPATCH: return "DISPATCH";
        case GpuProfiler::MarkKind::TRANSFER: return "TRANSFER";
    }
    return "?";
}

void GpuProfilerPanel::_copy_report(EditorContext& ctx)
{
    const Renderer& r = *ctx.renderer;
    const RenderView& v = r.main_view;
    const drivers::DeviceDriverVulkan& dd = *r.dd;
    const GpuProfiler& prof = v.graph.gpu_profiler;
    const std::vector<GpuProfiler::Timing>& results = prof.results;

    std::string out;
    out.reserve(16 * 1024);

    const VkPhysicalDeviceProperties& p = dd.physical_device_properties;
    _append(out, "=== Lumen GPU Profile ===\n");
    _append(out, "GPU: %s (vendor 0x%04x, %s)\n", p.deviceName, p.vendorID, string_VkPhysicalDeviceType(p.deviceType));
    _append(out, "Vulkan: %u.%u.%u, driver 0x%08x\n", VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion), p.driverVersion);
    _append(out, "Mesh shaders: %s, task shaders: %s\n", dd.capabilities.mesh.mesh_shader ? "yes" : "no", dd.capabilities.mesh.task_shader ? "yes" : "no");

    _append(out, "\n--- Output ---\n");
    const drivers::DeviceDriverVulkan::Swapchain& sc = dd.swapchain;
    if (!sc.images.empty()) _append(out, "Swapchain: %ux%u, %zu images, %s\n", sc.images[0].extent.width, sc.images[0].extent.height, sc.images.size(), string_VkFormat(sc.format));
    if (sc.surface) _append(out, "Present mode: %s (vsync mode %d)\n", string_VkPresentModeKHR(sc.surface->present_mode), (int)sc.surface->vsync_mode);
    _append(out, "Render resolution: %ux%u (%.2f MPix)\n", v.width, v.height, (double)v.width * v.height / 1e6);
    _append(out, "Frames in flight: %u, frame #%llu\n", r.frame_count, (unsigned long long)r.frame_number);

    _append(out, "\n--- Settings ---\n");
    _append(out, "GPU profiling: %s, pipeline stats: %s, breadcrumbs: %s, frozen: %s\n", ctx.profiling && ctx.profiling->gpu ? "on" : "off", prof.stats_active ? "on" : "off", v.graph.breadcrumbs ? "on" : "off", prof.frozen ? "yes" : "no");
    _append(out, "LOD bias: %.2f\n", v.lod_bias);
    if (ctx.render_path) {
        const GeometryFeature& geo = ctx.render_path->geometry;
        _append(out, "Occlusion: %s (HiZ ok %s, phase-1 history %s), contribution culling: %s (%.2f px)\n", geo.occlusion ? "on" : "off", geo.hiz_ok ? "yes" : "no", geo.hiz_use_prev ? "yes" : "no", geo.contribution_culling ? "on" : "off", geo.contribution_px);
        const uint32_t view = ctx.render_path->debug.view;
        _append(out, "Debug view: %s\n", view < (uint32_t)DEBUG_VIEW_COUNT ? DEBUG_VIEWS[view].name : "?");
    }

    _append(out, "\n--- Scene ---\n");
    _append(out, "Instances: %u (static %u), cluster ref capacity: %u\n", v.frame.instance_count, (uint32_t)v.frame.static_insts.instances.size(), v.frame.cluster_ref_capacity);
    _append(out, "Meshes loaded: %zu, cluster pool extent: %u\n", r.geometry.mesh_guids.size() - r.geometry.free_meshes.size(), r.geometry.cluster_extent);
    _append(out, "Lights: %u directional, %u local\n", v.frame.directional_count, v.frame.light_count > MAX_DIRECTIONAL_LIGHTS ? v.frame.light_count - MAX_DIRECTIONAL_LIGHTS : 0u);
    if (ctx.render_path && ctx.profiling && ctx.profiling->gpu_on()) {
        const GeometryFeature::CullStats& s = ctx.render_path->geometry.stats;
        _append(out, "Cull: refs %u, phase1 visible %u, retest %u, phase2 visible %u, instances visible %u / occluded %u / recovered %u\n", s.refs, s.phase1_visible, s.retest, s.phase2_visible, s.instances_visible, s.instances_occluded, s.instances_recovered);
    } else {
        _append(out, "Cull stats: off (enable pipeline statistics)\n");
    }
    if (ctx.render_path && ctx.render_path->geometry.compact_raster_frame) {
        const GeometryFeature& geo = ctx.render_path->geometry;
        _append(out, "Compact raster: capacity %u tris (%.1f MB), demand %u tris, spilled clusters %u, resizes %u\n", geo.compact_capacity, (double)geo.compact_capacity * 16.0 / (1024.0 * 1024.0), geo.compact_demand, geo.compact_spilled, geo.compact_resizes);
        _append(out, "Raster bins (capacity/demand tris): opaque %u/%u, two-sided %u/%u, masked %u/%u, masked two-sided %u/%u\n", geo.compact_bin_capacity[0], geo.compact_bin_demand[0], geo.compact_bin_capacity[1], geo.compact_bin_demand[1], geo.compact_bin_capacity[2], geo.compact_bin_demand[2], geo.compact_bin_capacity[3], geo.compact_bin_demand[3]);
    }

    _append(out, "\n--- CPU ---\n");
    const CpuProfiler& cpu = cpu_profiler();
    double cpu_frame_ms = 0.0;
    if (const CpuProfiler::Frame* f = cpu.last_complete_frame()) {
        cpu_frame_ms = (double)(f->end - f->begin) * cpu.ticks_to_ms;
        _append(out, "Frame: %.3f ms (%.1f fps)\n", cpu_frame_ms, cpu_frame_ms > 0.0 ? 1000.0 / cpu_frame_ms : 0.0);
        std::vector<const CpuProfiler::Zone*> zones;
        for (const CpuProfiler::Zone& z : cpu.zones) {
            if (z.lane != cpu.main_lane || z.depth > 2 || z.begin < f->begin || z.end > f->end) continue;
            zones.push_back(&z);
        }
        std::sort(zones.begin(), zones.end(), [](const CpuProfiler::Zone* a, const CpuProfiler::Zone* b) { return a->begin != b->begin ? a->begin < b->begin : a->depth < b->depth; });
        for (const CpuProfiler::Zone* z : zones) {
            char name[48];
            std::snprintf(name, sizeof(name), "%*s%s", (int)(z->depth * 2), "", z->name);
            _append(out, "  %-30s %8.3f ms%s\n", name, (double)(z->end - z->begin) * cpu.ticks_to_ms, (z->flags & CpuProfiler::FLAG_WAIT) ? "  (wait)" : "");
        }
    } else {
        _append(out, "No complete CPU frame recorded.\n");
    }

    _append(out, "\n--- GPU ---\n");
    if (results.empty()) {
        _append(out, "No GPU timing (enable GPU profiling).\n");
    } else {
        double pass_ms = 0.0, sync_ms = 0.0, gap_ms = 0.0;
        uint32_t pass_count = 0, sync_count = 0;
        std::vector<uint32_t> top;
        for (uint32_t i = 0; i < (uint32_t)results.size(); i++) {
            const GpuProfiler::Timing& t = results[i];
            if (t.parent != GpuProfiler::INVALID) continue;
            gap_ms += t.gap_ms;
            if (t.kind == GpuProfiler::MarkKind::BARRIER) { sync_ms += t.gpu_ms; sync_count++; }
            else { pass_ms += t.gpu_ms; pass_count++; top.push_back(i); }
        }
        _append(out, "Total: %.3f ms, draws: %u%s\n", prof.total_ms, prof.total_draws, prof.truncated ? " (TRUNCATED)" : "");
        _append(out, "Passes: %u = %.3f ms, sync: %u = %.3f ms, idle gaps: %.3f ms\n", pass_count, pass_ms, sync_count, sync_ms, gap_ms);
        if (cpu_frame_ms > 0.0) _append(out, "Graph GPU time vs CPU frame: %.3f / %.3f ms (%.0f%% of the frame is outside measured GPU work)\n", prof.total_ms, cpu_frame_ms, std::max(0.0, 100.0 * (1.0 - prof.total_ms / cpu_frame_ms)));

        std::sort(top.begin(), top.end(), [&](uint32_t a, uint32_t b) { return results[a].gpu_ms > results[b].gpu_ms; });
        _append(out, "\nTop passes:\n");
        for (uint32_t i = 0; i < (uint32_t)std::min<size_t>(top.size(), 10); i++) {
            const GpuProfiler::Timing& t = results[top[i]];
            _append(out, "  %-28s %8.3f ms  %5.1f%%\n", t.name, t.gpu_ms, prof.total_ms > 0.0 ? 100.0 * t.gpu_ms / prof.total_ms : 0.0);
        }

        _append(out, "\nTimeline (ms, gap before):\n");
        _append(out, "%-9s %-36s %-12s %9s %8s %6s %11s %11s %11s %7s\n", "kind", "name", "category", "gpu", "gap", "draws", "verts", "prims", "samples", "inst");
        for (uint32_t i = 0; i < (uint32_t)results.size(); i++) {
            const GpuProfiler::Timing& t = results[i];
            uint32_t depth = 0;
            for (uint32_t pi = t.parent; pi != GpuProfiler::INVALID && pi < results.size() && depth < 8; pi = results[pi].parent) depth++;
            char name[64];
            std::snprintf(name, sizeof(name), "%*s%s", (int)(depth * 2), "", t.name);
            _append(out, "%-9s %-36s %-12s %9.3f %8.3f %6u %11llu %11llu %11llu %7u\n", _kind_name(t.kind), name, t.kind == GpuProfiler::MarkKind::PASS || t.kind == GpuProfiler::MarkKind::BARRIER ? t.category : t.type, t.gpu_ms, t.gap_ms, t.draw_count, (unsigned long long)t.vertices, (unsigned long long)t.primitives, (unsigned long long)t.samples, t.instances);
        }
    }

    ImGui::SetClipboardText(out.c_str());
}

/*****************/
/**** TOOLBAR ****/
/*****************/

void GpuProfilerPanel::draw_contents(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();

    if (ImGui::Button(frozen ? ICON_FA_PLAY "  Resume" : ICON_FA_PAUSE "  Freeze")) frozen = !frozen;
    _draw_toolbar(ctx);
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_FA_CIRCLE_QUESTION);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        imgui_title("Controls");
        imgui_property_row("Zoom", "%s", "Mouse Scroll");
        imgui_property_row("Zoom Area", "%s", "Mouse Drag");
        imgui_property_row("Zoom Out", "%s", "Double Click");
        imgui_property_row("Pan", "%s", "ALT + Mouse / Middle Mouse");
        imgui_property_row("Pan Horizontal", "%s", "SHIFT + Mouse Scroll");
        imgui_property_row("Scroll Lanes", "%s", "CTRL + Mouse Scroll");
        imgui_property_row("Freeze", "%s", "Space");
        imgui_property_row("Resume", "%s", "ESC");
        ImGui::EndTooltip();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.chrome);
    ImGui::PushStyleColor(ImGuiCol_Border, ui.color.line);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("Graph", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) frozen = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) frozen = false;
    }

    _draw_graph(ctx);

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

void GpuProfilerPanel::_draw_empty(const char* p_text)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size = ImGui::CalcTextSize(p_text);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - size.x) * 0.5f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - size.y) * 0.5f);
    ImGui::TextDisabled("%s", p_text);
}

void GpuProfilerPanel::_draw_toolbar(EditorContext& ctx)
{
    const GpuProfiler& prof = ctx.renderer->main_view.graph.gpu_profiler;
    ProfilingSettings& settings = *ctx.profiling;

    ImGui::SameLine();
    ImGui::BeginDisabled(!prof.supported);
    ImGui::Checkbox("GPU Profiling", &settings.gpu);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Breadcrumbs", &ctx.renderer->main_view.graph.breadcrumbs);
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_COPY "  Copy Report")) _copy_report(ctx);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy passes, timings, counts, resolution and settings to the clipboard.");
}

/***************/
/**** GRAPH ****/
/***************/

void GpuProfilerPanel::_draw_graph(EditorContext& ctx)
{
    GpuProfiler& prof = ctx.renderer->main_view.graph.gpu_profiler;
    const UiStyle& ui = ui_style();
    prof.frozen = frozen;

    const std::vector<GpuProfiler::Timing>& src = prof.results;

    if (src.empty() || prof.result_frame == UINT64_MAX) {
        _draw_empty(ctx.profiling && ctx.profiling->gpu ? "No GPU timing yet." : "GPU profiling is off.");
        return;
    }

    const size_t n = src.size();
    const double tick_ms = cpu_profiler().ticks_to_ms;
    const int64_t frame_begin = prof.result_begin_ticks;
    start.resize(n);
    for (size_t i = 0; i < n; i++) start[i] = (float)((double)(src[i].begin_ticks - frame_begin) * tick_ms);
    const float frame_ms = (float)((double)(prof.result_end_ticks - frame_begin) * tick_ms);

    const float gap = ui.profiler.lane_gap;
    const float sec_h = ui.profiler.section_height;
    const float pass_h = ui.profiler.pass_height;
    const float barr_h = ui.profiler.barrier_height;
    const float draw_h = ui.profiler.draw_height;

    _canvas_begin("##gpu_timeline", frame_ms, sec_h + gap + pass_h + gap + draw_h);

    const float y_sec0 = origin.y;
    const float y_sec1 = y_sec0 + sec_h;
    const float y_pass0 = y_sec1 + gap;
    const float y_pass1 = y_pass0 + pass_h;
    const float y_barr0 = y_sec1 + gap;
    const float y_barr1 = y_pass0 + barr_h;
    const float y_draw0 = y_pass1 + gap;
    const float y_draw1 = y_draw0 + draw_h;

    // Categories.
    {
        const char* run_cat = nullptr;
        float run_begin = 0.0f;
        float run_end = 0.0f;

        auto flush = [&]() {
            if (!run_cat) return;
            const ImVec2 x = _bar(run_begin, run_end - run_begin, y_sec0, y_sec1, imgui_rg_category_u32(run_cat));
            _label(ImVec2(x.x, y_sec0), ImVec2(x.y, y_sec1), run_cat[0] ? run_cat : "(uncat)", IM_COL32_WHITE);
        };

        for (size_t i = 0; i < n; ++i) {
            const GpuProfiler::Timing& t = src[i];
            if (t.kind != GpuProfiler::MarkKind::PASS && t.kind != GpuProfiler::MarkKind::BARRIER) continue;
            const char* cat = t.category ? t.category : "";
            const float lead = start[i] - (float)t.gap_ms;
            const float end = start[i] + (float)t.gpu_ms;
            if (run_cat && std::strcmp(run_cat, cat) == 0) { run_end = end; continue; }
            flush();
            run_cat = cat;
            run_begin = lead;
            run_end = end;
        }
        flush();
    }

    // Passes.
    for (size_t i = 0; i < n; ++i) {
        const GpuProfiler::Timing& t = src[i];
        if (t.kind != GpuProfiler::MarkKind::PASS) continue;

        bool bar_hovered = false;
        const ImVec2 x = _bar(start[i], (float)t.gpu_ms, y_pass0, y_pass1, imgui_rg_category_u32(t.category), &bar_hovered);
        _label(ImVec2(x.x, y_pass0), ImVec2(x.y, y_pass1), t.name, IM_COL32_WHITE);

        if (bar_hovered) {
            ImGui::BeginTooltip();
            imgui_title("%s", t.name);
            imgui_property_row("Time", "%.0f µs", t.gpu_ms * 1000.0);
            imgui_property_row("Start", "%.3f ms", start[i]);
            imgui_property_row("Pixel Count", "%llu", (unsigned long long)t.samples);
            ImGui::EndTooltip();
        }
    }

    // Draw calls + compute dispatches.
    for (size_t i = 0; i < n; ++i) {
        const GpuProfiler::Timing& t = src[i];
        if (t.kind != GpuProfiler::MarkKind::DRAW && t.kind != GpuProfiler::MarkKind::DISPATCH && t.kind != GpuProfiler::MarkKind::TRANSFER) continue;

        const uint32_t p = t.parent;
        if (p == GpuProfiler::INVALID) continue;
        const float parent_end = start[p] + (float)src[p].gpu_ms;
        const float a_ms = start[i];
        if (a_ms >= parent_end) continue;
        const float len_ms = std::min((float)t.gpu_ms, parent_end - a_ms);

        bool bar_hovered = false;
        const ImVec2 x = _bar(a_ms, len_ms, y_draw0, y_draw1, imgui_rg_category_u32(src[p].category, ui.alpha.category_fill), &bar_hovered);

        const bool named = t.name && t.name[0];
        char lbl[16];
        if (!named) snprintf(lbl, sizeof(lbl), "%u", t.ordinal);
        _label(ImVec2(x.x, y_draw0), ImVec2(x.y, y_draw1), named ? t.name : lbl, IM_COL32_WHITE);

        if (bar_hovered) {
            ImGui::BeginTooltip();

            const bool is_graphics = t.kind == GpuProfiler::MarkKind::DRAW;
            if (named) {
                imgui_title("%s · %s", t.name, t.type);
            } else {
                const char* k = t.kind == GpuProfiler::MarkKind::DISPATCH ? "Dispatch" : t.kind == GpuProfiler::MarkKind::TRANSFER ? "Transfer" : "Draw";
                imgui_title("%s %u", k, t.ordinal);
            }
            imgui_property_row("Time", "%.0f µs", t.gpu_ms * 1000.0);
            if (is_graphics) imgui_property_row("Pixel Count", "%llu", (unsigned long long)t.samples);
            imgui_spacing();

            imgui_title("Owner Object");
            imgui_property_row("Pass", "%s", src[p].name);
            imgui_property_row("Draw Index", "%u", t.ordinal);
            imgui_property_row("Name", "%s", t.name);
            imgui_property_row("Location", "%s", "n/a");
            imgui_property_row("Type", "%s", t.type);
            imgui_spacing();

            if (is_graphics) {
                imgui_title("Primitives");
                imgui_property_row("Triangles", "%llu", (unsigned long long)t.primitives);
                imgui_property_row("Vertices", "%llu", (unsigned long long)t.vertices);
                imgui_property_row("Instances", "%u", t.instances);
                imgui_property_row("Total Triangles", "%llu", (unsigned long long)src[p].primitives);
            }

            ImGui::EndTooltip();
        }
    }

    // Barriers.
    for (size_t i = 0; i < n; ++i) {
        const GpuProfiler::Timing& t = src[i];
        if (t.kind != GpuProfiler::MarkKind::BARRIER) continue;
        if (t.gpu_ms <= 0.0) continue;

        bool bar_hovered = false;
        _bar(start[i], (float)t.gpu_ms, y_barr0, y_barr1, ui.color.viz_barrier, &bar_hovered);
        if (bar_hovered) {
            ImGui::BeginTooltip();
            imgui_title("Syncronization · before %s", t.name);
            imgui_property_row("Time", "%.0f µs", t.gpu_ms * 1000.0);
            imgui_property_row("Gap", "%.0f µs", t.gap_ms * 1000.0);
            ImGui::EndTooltip();
        }
    }

    char status[64];
    snprintf(status, sizeof(status), "%s · GPU Frame %llu · %.3f ms", frozen ? "Frozen" : "Live View", (unsigned long long)prof.result_frame, frame_ms);
    _canvas_end(status);
}

/****************/
/**** CANVAS ****/
/****************/

void GpuProfilerPanel::_canvas_begin(const char* p_id, float p_total_ms, float p_content_h)
{
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float ruler_h = ImGui::GetTextLineHeight();

    total_ms = std::max(p_total_ms, MIN_RANGE_MS);
    canvas_w = std::max(avail.x, 1.0f);
    height = std::max(avail.y, p_content_h + ruler_h);
    dl = ImGui::GetWindowDrawList();
    origin = ImGui::GetCursorScreenPos();

    float range_ms = std::clamp(visible_range > 0.0f ? visible_range : total_ms, MIN_RANGE_MS, total_ms);
    visible_start = std::clamp(visible_start, 0.0f, std::max(0.0f, total_ms - range_ms));
    px = canvas_w / range_ms;

    ImGui::InvisibleButton(p_id, ImVec2(canvas_w, height), ImGuiButtonFlags_MouseButtonLeft);
    hovered = ImGui::IsItemHovered();
    view_top = std::max(origin.y, ImGui::GetWindowPos().y);
    view_bottom = std::min(origin.y + height, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y);

    dl->PushClipRect(origin, ImVec2(origin.x + canvas_w, origin.y + height), true);

    _canvas_input(range_ms);

    range_ms = std::clamp(visible_range > 0.0f ? visible_range : total_ms, MIN_RANGE_MS, total_ms);
    visible_start = std::clamp(visible_start, 0.0f, std::max(0.0f, total_ms - range_ms));
    px = canvas_w / range_ms;
    dragging = panning || ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle);

    _canvas_grid();
}

void GpuProfilerPanel::_canvas_input(float p_range_ms)
{
    const ImGuiIO& io = ImGui::GetIO();
    const UiStyle& ui = ui_style();

    if (hovered && io.MouseWheel != 0.0f && io.KeyCtrl) {
        ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * (ui.profiler.lane_header_height + ui.profiler.zone_height) * 3.0f);
    } else if (hovered && io.MouseWheel != 0.0f) {
        const float cursor_ms = visible_start + (io.MousePos.x - origin.x) / px;
        const float cursor_frac = (io.MousePos.x - origin.x) / canvas_w;
        const float new_range = std::clamp(p_range_ms * powf(0.85f, io.MouseWheel), MIN_RANGE_MS, total_ms);
        visible_start = cursor_ms - cursor_frac * new_range;
        visible_range = new_range;
    }

    if (hovered && io.MouseWheelH != 0.0f && visible_range > 0.0f) visible_start -= io.MouseWheelH * p_range_ms * 0.1f;

    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        _view_reset();
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (io.KeyAlt) {
            panning = true;
        } else {
            selecting = true;
            select_start_ms = visible_start + (io.MousePos.x - origin.x) / px;
            select_end_ms = select_start_ms;
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning = true;

    if (panning) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) visible_start -= io.MouseDelta.x / px;
        else panning = false;
    }

    if (selecting) {
        select_end_ms = std::clamp(visible_start + (io.MousePos.x - origin.x) / px, 0.0f, total_ms);
        select_start_ms = std::clamp(select_start_ms, 0.0f, total_ms);
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            selecting = false;
            const float a = std::min(select_start_ms, select_end_ms);
            const float b = std::max(select_start_ms, select_end_ms);
            if (b - a > 0.01f) {
                visible_start = a;
                visible_range = b - a;
            }
        }
    }
}

void GpuProfilerPanel::_canvas_grid()
{
    const UiStyle& ui = ui_style();
    const float visible_ms = canvas_w / px;
    const float block_ms = visible_ms >= 1.5f ? 1.0f : visible_ms >= 0.15f ? 0.1f : 0.01f;
    const float end_ms = _visible_end();
    const float ruler_h = ImGui::GetTextLineHeight();

    for (int i = (int)floorf(visible_start / block_ms); ; i++) {
        const float t = (float)i * block_ms;
        if (t > end_ms) break;
        const float x0 = _x_of(t);
        const float x1 = _x_of(t + block_ms);
        dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + height), (i & 1) == 0 ? ui.color.viz_stripe_even : ui.color.viz_stripe_odd);

        char text[32];
        if (block_ms == 1.0f) snprintf(text, sizeof(text), "%.0f ms", t);
        else if (block_ms == 0.1f) snprintf(text, sizeof(text), "%.1f ms", t);
        else snprintf(text, sizeof(text), "%.2f ms", t);
        dl->AddText(ImVec2(x0 + ui.profiler.ruler_text_pad, view_bottom - ruler_h), ui.color.viz_label, text);
    }
}

void GpuProfilerPanel::_canvas_end(const char* p_status)
{
    const UiStyle& ui = ui_style();
    const ImGuiIO& io = ImGui::GetIO();

    if (selecting) {
        const float a = std::min(select_start_ms, select_end_ms);
        const float b = std::max(select_start_ms, select_end_ms);
        dl->AddRectFilled(ImVec2(_x_of(a), origin.y), ImVec2(_x_of(b), origin.y + height), ui.color.selection);
    }

    if (hovered) dl->AddLine(ImVec2(io.MousePos.x, origin.y), ImVec2(io.MousePos.x, origin.y + height), ui.color.viz_cursor, ui.profiler.cursor_stroke);

    if (p_status && p_status[0]) {
        const ImVec2 ts = ImGui::CalcTextSize(p_status);
        const ImVec2 sp = ui.profiler.status_pad;
        const float tx = origin.x + ui.profiler.label_pad;
        const float ty = view_bottom - ts.y - sp.x - ImGui::GetTextLineHeight();
        dl->AddRectFilled(ImVec2(tx - sp.x, ty - sp.y), ImVec2(tx + ts.x + sp.x, ty + ts.y + sp.y), ui.color.overlay_label);
        dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ImGuiCol_TextDisabled), p_status);
    }

    dl->PopClipRect();
}

/**************/
/**** VIEW ****/
/**************/

void GpuProfilerPanel::_view_reset()
{
    visible_range = -1.0f;
    visible_start = 0.0f;
    selecting = false;
    panning = false;
}

float GpuProfilerPanel::_visible_end() const
{
    return visible_start + std::clamp(visible_range > 0.0f ? visible_range : total_ms, MIN_RANGE_MS, total_ms);
}

/**************/
/**** BARS ****/
/**************/

ImVec2 GpuProfilerPanel::_bar(float p_ms, float p_len_ms, float p_y0, float p_y1, ImU32 p_fill, bool* r_hovered)
{
    const UiStyle& ui = ui_style();
    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 a(_x_of(p_ms) + ui.profiler.bar_inset, p_y0);
    const ImVec2 b(_x_of(p_ms + p_len_ms) - ui.profiler.bar_inset, p_y1);
    const bool in_canvas = io.MousePos.x >= origin.x && io.MousePos.x <= origin.x + canvas_w && io.MousePos.y >= view_top && io.MousePos.y <= view_bottom;
    const bool is_hovered = in_canvas && !dragging && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y;
    if (r_hovered) *r_hovered = is_hovered;
    if (b.x > a.x) dl->AddRectFilled(a, b, (r_hovered && is_hovered) ? ui.color.viz_hovered : p_fill, ui.profiler.bar_rounding);
    return ImVec2(a.x, b.x);
}

void GpuProfilerPanel::_label(ImVec2 p_a, ImVec2 p_b, const char* p_text, ImU32 p_color)
{
    if (!p_text || !p_text[0]) return;
    const UiStyle& ui = ui_style();
    if (p_b.x - p_a.x < ui.profiler.label_min_width) return;
    const float lp = ui.profiler.label_pad;
    const ImVec2 ts = ImGui::CalcTextSize(p_text);
    const float x = std::max(p_a.x + lp, origin.x + lp);
    dl->PushClipRect(p_a, p_b, true);
    const ImVec2 p(x, p_a.y + ((p_b.y - p_a.y) - ts.y) * 0.5f);
    dl->AddText(ImVec2(p.x + ui.stroke.hairline, p.y + ui.stroke.hairline), ui.color.shadow_text, p_text);
    dl->AddText(p, p_color, p_text);
    dl->PopClipRect();
}

void GpuProfilerPanel::_marker(float p_ms, float p_y0, float p_y1, ImU32 p_color)
{
    const float x = _x_of(p_ms);
    if (x < origin.x || x > origin.x + canvas_w) return;
    dl->AddLine(ImVec2(x, p_y0), ImVec2(x, p_y1), p_color, ui_style().profiler.marker_stroke);
}

}
