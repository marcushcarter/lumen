#pragma once
#include <editor/docking/center_view/debugger.h>
#include <editor/docking/center_view/overlay_bar.h>
#include <editor/editor_context.h>
#include <editor/editor_camera.h>
#include <imgui.h>
#include <cstdint>

namespace lumen {

struct CenterView
{
    Debugger debugger;
    OverlayBar left_overlay;
    OverlayBar right_overlay;

    int selected_view = 0;
    float split_ratio = 0.66f;
    float screen_percentage = 1.0f;

    void initialize();

    bool _view_item(const char* p_name, int p_id);
    bool _view_submenu(const char* p_category, bool p_active);
    void _draw_scene(EditorContext& ctx);
    void draw(EditorContext& ctx);
};

}