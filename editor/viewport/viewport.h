#pragma once
#include <core/physics/collision_preview.h>
#include <core/game/entity_debug.h>
#include <editor/viewport/transform_gizmo.h>
#include <editor/viewport/shape_gizmo.h>
#include <editor/application/editor_context.h>
#include <core/scene/scene.h>
#include <drivers/windows/cursor_lock_win32.h>
#include <imgui.h>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen {

struct EditorPlay;

enum class ViewOverlay : uint8_t { NONE, COLLISION, BONES, DEBUG_LINES, EVERYTHING, COUNT };

struct ViewportView
{
    ViewOverlay overlay = ViewOverlay::NONE;
    int debug_view = 0;
    bool culling_paused = false;
    bool stats = false;
};

const char* view_overlay_name(ViewOverlay p_overlay);
uint32_t view_overlay_kinds(ViewOverlay p_overlay);
bool view_overlay_lines(ViewOverlay p_overlay);

ViewportView& viewport_view();

struct Viewport
{
    static constexpr float CULL_FRUSTUM_DISTANCE = 40.0f;

    TransformGizmo gizmo;
    ShapeGizmo shape_gizmo;
    drivers::CursorLockWin32 cursor_lock;
    CollisionMeshCache collision_meshes;
    std::vector<Entity> _view_selection;
    std::vector<Entity> debug_entities;
    const EditorPlay* play = nullptr;

    bool gizmo_world = true;
    bool popup_open_last_frame = false;
    bool flying = false;
    ImVec2 fly_look = ImVec2(0.0f, 0.0f);
    float fly_wheel = 0.0f;
    bool game_captured = false;
    ImVec2 game_look = ImVec2(0.0f, 0.0f);

    static constexpr uint32_t DROP_TIMEOUT_FRAMES = 8;

    struct PendingDrop
    {
        bool active = false;
        bool is_template = false;
        Guid guid;
        vec3 origin = vec3(0.0f);
        vec3 direction = vec3(0.0f, 0.0f, -1.0f);
        uint32_t frames = 0;
    } drop;

    void _finish_drop(EditorContext& ctx, float p_depth, bool p_hit);

    void _submit_debug_draw(EditorContext& ctx, bool p_live);
    void _draw_cull_frustum(EditorContext& ctx);
    bool _draw_gizmo(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size, bool p_hover_ok);
    static void draw_bar(ImVec2 p_min, ImVec2 p_max, bool p_overlay = true);
    static void draw_stats(EditorContext& ctx, ImVec2 p_pos);
    void _draw_image(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size);
    void _update_fly(bool p_start);
    void _asset_drop(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size);

    static void draw_options_menu(EditorContext& ctx);
    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
};

}
