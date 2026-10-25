#pragma once
#include <editor/layout/panel.h>
#include <imgui.h>
#include <cstdint>

namespace lumen {

// Shared base for the CPU and GPU profiler tabs: toolbar row (freeze, tab items, controls help) above a zoom/pan/select
// timeline canvas. Time is in ms from 0 to total_ms. Per draw: _canvas_begin() -> _bar()/_label()/_marker() -> _canvas_end().
struct ProfilerPanelBase : Panel
{
    static constexpr float MIN_RANGE_MS = 0.01f;

    float visible_start = 0.0f;
    float visible_range = -1.0f;
    bool selecting = false;
    float select_start_ms = 0.0f;
    float select_end_ms = 0.0f;
    bool panning = false;
    bool frozen = false;

    ImDrawList* dl = nullptr;
    ImVec2 origin = {};
    float canvas_w = 0.0f;
    float height = 0.0f;
    float total_ms = 0.0f;
    float px = 1.0f;
    float view_top = 0.0f;
    float view_bottom = 0.0f;
    bool hovered = false;
    bool dragging = false;

    DockZone default_zone() const override { return DockZone::BOTTOM; }
    bool default_open() const override { return false; }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar; }
    ImVec2 window_padding() const override { return ui_style().dock.drawer_padding; }
    void draw_contents(EditorContext& ctx) override;

    virtual void _draw_toolbar(EditorContext& ctx) { (void)ctx; }
    virtual void _draw_graph(EditorContext& ctx) = 0;

    void _draw_empty(const char* p_text);
    void _canvas_begin(const char* p_id, float p_total_ms, float p_content_h);
    void _canvas_end(const char* p_status);
    void _canvas_input(float p_range_ms);
    void _canvas_grid();
    void _view_reset();

    float _x_of(float p_ms) const { return origin.x + (p_ms - visible_start) * px; }
    float _visible_end() const;

    ImVec2 _bar(float p_ms, float p_len_ms, float p_y0, float p_y1, ImU32 p_fill, bool* r_hovered = nullptr);
    void _label(ImVec2 p_a, ImVec2 p_b, const char* p_text, ImU32 p_color);
    void _marker(float p_ms, float p_y0, float p_y1, ImU32 p_color);
};

}
