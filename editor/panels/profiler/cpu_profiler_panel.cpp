#include <editor/panels/profiler/cpu_profiler_panel.h>
#include <drivers/imgui/ui_style.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/base/cpu_profiler.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace lumen {

/*****************/
/**** CAPTURE ****/
/*****************/

void CpuProfilerPanel::_capture_update()
{
    if (frozen) return;
    const CpuProfiler& cpu = cpu_profiler();
    const CpuProfiler::Frame* f = cpu.last_complete_frame();
    if (!f || capture.frame == f->number) return;

    capture.frame = f->number;

    const double tick_ms = cpu.ticks_to_ms;
    const int64_t origin_ticks = f->begin;
    auto to_ms = [&](int64_t p_ticks) { return (float)((double)(p_ticks - origin_ticks) * tick_ms); };
    capture.total_ms = to_ms(f->end);

    // Only work that ran during the frame; zones crossing the frame edges are clipped to it.
    const uint32_t lane_count = cpu.lane_count.load(std::memory_order_acquire);
    std::vector<int32_t> lane_rows(lane_count, 0);
    capture.zones.clear();
    for (const CpuProfiler::Zone& z : cpu.zones) {
        if (z.end <= f->begin || z.begin >= f->end || z.lane >= lane_count) continue;
        capture.zones.push_back({ z.name, to_ms(std::max(z.begin, f->begin)), to_ms(std::min(z.end, f->end)), z.lane, z.depth, z.flags });
        lane_rows[z.lane] = std::max(lane_rows[z.lane], (int32_t)z.depth + 1);
    }

    capture.lanes.clear();
    auto add_lane = [&](uint32_t p_lane) {
        if (lane_rows[p_lane] == 0) return;
        Capture::Lane l{};
        l.index = p_lane;
        l.rows = (uint32_t)lane_rows[p_lane];
        std::snprintf(l.name, sizeof(l.name), "%s", cpu.lane_name(p_lane));
        capture.lanes.push_back(l);
    };
    if (cpu.main_lane < lane_count) add_lane(cpu.main_lane);
    for (uint32_t i = 0; i < lane_count; i++) if (i != cpu.main_lane) add_lane(i);
}

/***************/
/**** GRAPH ****/
/***************/

void CpuProfilerPanel::_draw_graph(EditorContext& ctx)
{
    (void)ctx;
    const UiStyle& ui = ui_style();
    _capture_update();

    if (capture.frame == UINT64_MAX || capture.total_ms <= 0.0f) {
        _draw_empty("No CPU frame recorded yet.");
        return;
    }

    const float gap = ui.profiler.lane_gap;
    const float header_h = ui.profiler.lane_header_height;
    const float zone_h = ui.profiler.zone_height;
    const float block_gap = ui.profiler.section_gap;

    float content_h = 0.0f;
    for (const Capture::Lane& l : capture.lanes) content_h += header_h + (float)l.rows * (zone_h + gap) + block_gap;

    _canvas_begin("##cpu_timeline", capture.total_ms, content_h);

    lane_y.assign(cpu_profiler().lane_count.load(std::memory_order_acquire) + 1, -1.0f);
    float y = origin.y;
    for (const Capture::Lane& l : capture.lanes) {
        dl->AddText(ImVec2(origin.x + ui.profiler.label_pad, y), ui.color.viz_label, l.name);
        y += header_h;
        if (l.index < lane_y.size()) lane_y[l.index] = y;
        y += (float)l.rows * (zone_h + gap) + block_gap;
    }

    const float vis_end = _visible_end();
    for (const Capture::Zone& z : capture.zones) {
        if (z.lane >= lane_y.size() || lane_y[z.lane] < 0.0f) continue;
        if (z.end_ms < visible_start || z.begin_ms > vis_end) continue;
        const float y0 = lane_y[z.lane] + (float)z.depth * (zone_h + gap);
        const float y1 = y0 + zone_h;
        if (y1 < view_top || y0 > view_bottom) continue;

        const bool wait = (z.flags & CpuProfiler::FLAG_WAIT) != 0;
        bool bar_hovered = false;
        const ImVec2 x = _bar(z.begin_ms, z.end_ms - z.begin_ms, y0, y1, wait ? ui.color.viz_wait : imgui_rg_category_u32(z.name), &bar_hovered);
        _label(ImVec2(x.x, y0), ImVec2(x.y, y1), z.name, wait ? ui.color.viz_label : IM_COL32_WHITE);

        if (bar_hovered) {
            ImGui::BeginTooltip();
            imgui_title("%s", z.name);
            imgui_property_row("Time", "%.3f ms", z.end_ms - z.begin_ms);
            imgui_property_row("Start", "%.3f ms", z.begin_ms);
            imgui_property_row("Thread", "%s", cpu_profiler().lane_name(z.lane));
            imgui_property_row("Depth", "%u", (uint32_t)z.depth);
            if (wait) imgui_property_row("Kind", "%s", "Wait");
            ImGui::EndTooltip();
        }
    }

    char status[96];
    snprintf(status, sizeof(status), "%s · CPU Frame %llu · %.3f ms", frozen ? "Frozen" : "Live View", (unsigned long long)capture.frame, capture.total_ms);
    _canvas_end(status);
}

}
