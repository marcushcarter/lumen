#pragma once
#include <imgui.h>

namespace lumen {

struct OverlayBar
{
    enum class Align { LEFT, RIGHT };

    ImVec2 origin;
    ImVec2 region;
    Align align;
    float margin;
    float spacing;
    float row_y;
    float cursor_x;
    ImU32 active_col;

    void begin(ImVec2 p_origin, ImVec2 p_region, Align p_align, float p_margin = 8.0f, float p_spacing = 4.0f);
    void end();

    bool _emit(const char* p_label, ImVec2 p_size, bool p_active);
    
    bool button(const char* p_label, ImVec2 p_size = ImVec2(0, 0));
    bool toggle(const char* p_label, bool& p_active, ImVec2 p_size = ImVec2(0, 0));
    bool combo(const char* id, const char* preview, float width);
    bool begin_menu(const char* p_label, ImVec2 p_size = ImVec2(0,0));
    void end_menu();

    void gap(float p_w);
};

}