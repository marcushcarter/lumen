#pragma once
#include <editor/docking/center_view/debugger.h>
#include <editor/docking/center_view/overlay_bar.h>
#include <editor/editor_context.h>
#include <editor/editor_camera.h>
#include <imgui.h>
#include <ImGuizmo.h>
#include <cstdint>

namespace lumen {

struct DebugViewCategory;

struct CenterView
{
    static constexpr float MIN_DEBUG = 0.025f;
    static constexpr float MAX_DEBUG = 0.7f;

    Debugger debugger;
    OverlayBar left_overlay;
    OverlayBar right_overlay;

    int selected_view = 0;
    float split_ratio = 1.0f;
    float usable_h = 1.0f;
    float scene_h = 0.0f;
    float content_h = 0.0f;
    float screen_percentage = 1.0f;
    float item_w = 240.0f;
    
    bool gizmo_world = true;
    ImGuizmo::OPERATION gizmo_op = ImGuizmo::TRANSLATE;
    bool popup_open_last_frame = false;

    void initialize();

    float layout(ImVec2 p_min, ImVec2 p_max);
    void apply_split_delta(float p_delta, bool p_activated);

    bool _view_item(const char* p_icon, const char* p_name, int p_id);
    bool _view_submenu(const DebugViewCategory& p_category, bool p_active);
    bool _draw_gizmo(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size);
    void _draw_scene(EditorContext& ctx);
    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
};

}
