#pragma once
#include <editor/viewport/overlay_bar.h>
#include <editor/viewport/transform_gizmo.h>
#include <editor/editor_context.h>
#include <imgui.h>
#include <cstdint>

namespace lumen {

struct DebugViewCategory;

struct Viewport
{
    OverlayBar left_overlay;
    OverlayBar right_overlay;
    TransformGizmo gizmo;

    int selected_view = 0;
    float screen_percentage = 1.0f;
    float item_w = 240.0f;
    bool gizmo_world = true;
    bool popup_open_last_frame = false;

    bool allow_pick = true;
    bool allow_outline = true;
    bool allow_gizmo = true;

    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);

    bool _view_item(const char* p_icon, const char* p_name, int p_id);
    bool _view_submenu(const DebugViewCategory& p_category, bool p_active);
    bool _draw_gizmo(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size, bool p_hover_ok);
    void _draw_image(EditorContext& ctx);
};

}