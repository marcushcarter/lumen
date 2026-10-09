#pragma once
#include <imgui.h>
#include <cstddef>

namespace lumen {

inline constexpr float DOCK_GAP = 4.0f;

struct DockColors
{
    ImU32 gap;
    ImU32 strip;
    ImU32 pane;
    ImU32 tab_hovered;
    ImU32 line;
    ImU32 accent;
    ImU32 text;
    ImU32 text_dim;
    ImU32 drop_fill;
    ImU32 drop_line;

    static DockColors get();
};

void dock_menu_push_style();
void dock_menu_pop_style();
void dock_field_push_style();
void dock_field_pop_style();
bool dock_search_bar(const char* p_id, char* p_buf, size_t p_size, float p_width);

struct TabStrip
{
    enum class Edge { TOP, BOTTOM };

    static constexpr float PAD_X = 12.0f;
    static constexpr float MIN_TAB_W = 36.0f;
    static constexpr float ACCENT_H = 2.0f;

    DockColors colors;
    ImDrawList* draw_list = nullptr;
    ImVec2 min;
    ImVec2 max;
    Edge edge = Edge::TOP;
    float scale = 1.0f;
    float cursor_x = 0.0f;
    float trailing_x = 0.0f;

    static float height();
    static float natural_width(const char* p_label);

    void begin(const char* p_id, ImVec2 p_min, ImVec2 p_max, Edge p_edge, float p_natural_total, float p_reserved_right = 0.0f, ImU32 p_bg = 0);
    bool tab(const char* p_label, bool p_selected);
    bool trailing_button(const char* p_icon);
    void end();
};

}
