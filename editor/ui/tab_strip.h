#pragma once
#include <imgui.h>
#include <cstddef>

namespace lumen {

struct TabStrip
{
    enum class Edge { TOP, BOTTOM };

    ImDrawList* draw_list = nullptr;
    ImU32 background = 0;
    ImVec2 min;
    ImVec2 max;
    Edge edge = Edge::TOP;
    float scale = 1.0f;
    float cursor_x = 0.0f;
    float trailing_x = 0.0f;
    float natural_total = 0.0f;
    float avail = 0.0f;
    float* scroll = nullptr;
    bool reveal = false;

    static float height();
    static float natural_width(const char* p_label);
    static float close_size();
    static float dock_height();
    static float dock_natural_width(const char* p_label, bool p_closable = true);

    void begin(const char* p_id, ImVec2 p_min, ImVec2 p_max, Edge p_edge, float p_natural_total, float p_reserved_right = 0.0f, ImU32 p_bg = 0);
    bool tab(const char* p_label, bool p_selected);
    bool button(const char* p_label, bool p_on);
    bool dock_tab(const char* p_label, bool p_selected, bool* p_close, bool p_dirty = false);
    bool trailing_button(const char* p_icon);
    bool console_field(const char* p_id, const char* p_label, const char* p_hint, char* p_buf, size_t p_size, float p_field_width);
    void scrollable(float& r_scroll, bool p_reveal, float p_lead);
    void end();

    void _draw_label(ImVec2 p_a, ImVec2 p_b, const char* p_label, bool p_bright);
};

}
