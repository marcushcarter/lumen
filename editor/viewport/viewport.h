#pragma once
#include <editor/viewport/overlay_bar.h>
#include <editor/viewport/transform_gizmo.h>
#include <editor/application/editor_context.h>
#include <core/world/world.h>
#include <drivers/windows/cursor_lock_win32.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <cstdint>
#include <functional>
#include <vector>

namespace lumen {

struct DebugViewCategory;
struct EditorPlay;

struct ViewportView
{
    int debug_view = 0;
    bool show_bones = true;
    bool show_collision = false;
    bool show_debug_lines = true;
};

ViewportView& viewport_view();

struct Viewport
{
    static constexpr float LOOK_CLICK_SLOP = 3.0f;

    OverlayBar left_overlay;
    OverlayBar right_overlay;
    OverlayBar corner_overlay;
    TransformGizmo gizmo;
    drivers::CursorLockWin32 cursor_lock;

    float item_w = 240.0f;
    bool gizmo_world = true;
    bool popup_open_last_frame = false;

    bool allow_pick = true;
    bool allow_outline = true;
    bool allow_gizmo = true;
    bool allow_look = false;
    bool allow_grid = true;
    bool allow_asset_drop = true;
    bool allow_debug_draw = false;
    bool allow_play_capture = false;
    bool has_bones = false;
    bool has_collision = false;
    const EditorPlay* play = nullptr;
    std::vector<Entity> _view_selection;

    std::function<void(ImDrawList* p_draw_list, ImVec2 p_pos, ImVec2 p_size)> overlay;
    std::function<void()> options_controls;
    std::function<void()> debug_lines;

    bool looking = false;
    ImVec2 look_delta = ImVec2(0.0f, 0.0f);
    float look_zoom = 0.0f;
    bool look_clicked = false;
    ImVec2 look_click_pos = ImVec2(0.0f, 0.0f);
    ImVec2 _look_press = ImVec2(0.0f, 0.0f);
    float _look_travel = 0.0f;

    int _header_frame = -1;
    ImDrawFlags _corners = ImDrawFlags_RoundCornersAll;

    bool _view_item(const char* p_icon, const char* p_name, int p_id);
    void _draw_controls(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max, float p_margin);
    void _draw_options(EditorContext& ctx);
    void _draw_view_menu();
    uint32_t _debug_mask() const;
    void _submit_debug_draw(EditorContext& ctx, uint32_t p_mask, bool p_live);
    bool _view_submenu(const DebugViewCategory& p_category, bool p_active);
    bool _draw_gizmo(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size, bool p_hover_ok);
    void _draw_image(EditorContext& ctx);
    void _update_look(bool p_start, ImVec2 p_center);
    void _asset_drop(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size);

    void draw_header_controls(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
};

}
