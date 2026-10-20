#pragma once
#include <editor/viewport/overlay_bar.h>
#include <editor/viewport/transform_gizmo.h>
#include <editor/editor_context.h>
#include <drivers/windows/cursor_lock_win32.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <cstdint>

namespace lumen {

struct DebugViewCategory;

struct Viewport
{
    static constexpr float LOOK_CLICK_SLOP = 3.0f;

    struct TimePreset { const char* icon; const char* name; float time; };
    static constexpr TimePreset TIME_PRESETS[] = {
        { ICON_FA_SUN, "Noon", 12.0f },
        { ICON_FA_SUN, "Golden Hour", 18.25f },
        { ICON_FA_MOON, "Midnight", 0.0f },
        { ICON_FA_SUN, "Sunrise", 5.1f },
        { ICON_FA_SUN, "Sunset", 18.9f },
    };

    OverlayBar left_overlay;
    OverlayBar right_overlay;
    TransformGizmo gizmo;
    drivers::CursorLockWin32 cursor_lock;

    int selected_view = 0;
    float screen_percentage = 1.0f;
    float item_w = 240.0f;
    bool gizmo_world = true;
    bool popup_open_last_frame = false;

    bool allow_pick = true;
    bool allow_outline = true;
    bool allow_gizmo = true;
    bool allow_look = false;
    float* time_of_day = nullptr;

    bool looking = false;
    ImVec2 look_delta = ImVec2(0.0f, 0.0f);
    float look_zoom = 0.0f;
    ImVec2 _look_press = ImVec2(0.0f, 0.0f);
    float _look_travel = 0.0f;

    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);

    bool _view_item(const char* p_icon, const char* p_name, int p_id);
    bool _view_submenu(const DebugViewCategory& p_category, bool p_active);
    bool _draw_gizmo(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size, bool p_hover_ok);
    void _draw_image(EditorContext& ctx);
    void _update_look(bool p_start, ImVec2 p_center);
};

}