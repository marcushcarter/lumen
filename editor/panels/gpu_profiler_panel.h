#pragma once
#include <core/rendering/gpu_profiler.h>
#include <editor/layout/panel.h>
#include <imgui.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct GpuProfilerPanel : Panel
{
    static constexpr float MIN_RANGE_MS = 0.01f;

    struct Canvas {
        ImDrawList* dl = nullptr;
        ImVec2 origin = {};
        float width = 0.0f;
        float height = 0.0f;
        float total_ms = 0.0f;
        float px = 1.0f;
        float view_top = 0.0f;
        float view_bottom = 0.0f;
        bool hovered = false;
        bool dragging = false;
    };

    std::vector<float> start;
    float visible_start = 0.0f;
    float visible_range = -1.0f;
    float select_start_ms = 0.0f;
    float select_end_ms = 0.0f;
    bool selecting = false;
    bool panning = false;
    bool frozen = false;
    Canvas _canvas;

    void _canvas_begin(const char* p_id, float p_total_ms, float p_content_h);
    void _canvas_end(const char* p_status);
    float _visible_range() const;
    float _x_of(float p_ms) const { return _canvas.origin.x + (p_ms - visible_start) * _canvas.px; }
    ImVec2 _bar(float p_ms, float p_len_ms, float p_y0, float p_y1, ImU32 p_fill, bool* r_hovered = nullptr);
    void _label(ImVec2 p_a, ImVec2 p_b, const char* p_text, ImU32 p_color);

    void _copy_report(EditorContext& ctx);
    void _draw_toolbar(EditorContext& ctx);
    void _draw_timeline(EditorContext& ctx);

    const char* name() const override { return "GPU Profiler"; }
    DockZone default_zone() const override { return DockZone::BOTTOM; }
    bool default_open() const override { return false; }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar; }
    ImVec2 window_padding() const override { return ui_style().dock.drawer_padding; }
    void draw_contents(EditorContext& ctx) override;
};

}
