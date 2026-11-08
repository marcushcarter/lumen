#pragma once
#include <imgui.h>

namespace lumen {

struct EditorContext;
struct EditorTab;

struct TabToolbar
{
    ImVec2 min;
    ImVec2 max;
    float x = 0.0f;

    float _row_y() const;

    void begin();
    void end();
    bool button(const char* p_label, const char* p_tooltip = nullptr, bool p_active = false, bool p_enabled = true);
    bool menu(const char* p_label, bool p_enabled = true);
    void end_menu();
    bool dropdown(const char* p_id, const char* p_preview, float p_width = 0.0f);
    void end_dropdown();
    bool save(EditorContext& ctx, EditorTab& r_tab);
    void gap();
    void item(float p_width);
    void text(const char* p_text);
    ImVec2 inner_max() const;
};

}
