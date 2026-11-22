#pragma once
#include <core/physics/collision_preview.h>
#include <core/game/entity_debug.h>
#include <editor/viewport/transform_gizmo.h>
#include <editor/viewport/shape_gizmo.h>
#include <editor/viewport/orbit_camera.h>
#include <editor/play/shape_spawner.h>
#include <editor/application/editor_context.h>
#include <core/scene/scene.h>
#include <drivers/windows/cursor_lock_win32.h>
#include <imgui.h>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen {

struct EditorPlay;

enum class ViewOverlay : uint8_t { NONE, COLLISION, BONES, DEBUG_LINES, BUOYANCY, WATER_FLOW, COUNT };
enum class ViewAction : uint8_t { SELECT, MOVE, ROTATE, SCALE, ALL, COUNT };
enum class SimTool : uint8_t { SPAWN, GRAB, COUNT };

struct ViewportView
{
    ViewOverlay overlay = ViewOverlay::NONE;
    int debug_view = 0;
    bool culling_paused = false;
    bool stats = false;
    ViewAction action = ViewAction::MOVE;
    SimTool sim_tool = SimTool::GRAB;
    SpawnShape spawn_shape = SpawnShape::RANDOM;
};

uint8_t view_action_gizmo_parts(ViewAction p_action);

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
    ShapeSpawner spawner;
    OrbitDrag camera_drag;
    OrbitCamera::Input camera_input;
    float camera_view_height = 0.0f;
    bool game_captured = false;
    ImVec2 game_look = ImVec2(0.0f, 0.0f);

    static constexpr uint32_t DROP_TIMEOUT_FRAMES = 8;
    static constexpr float GRAB_STIFFNESS = 12.0f;
    static constexpr float GRAB_RESPONSE = 0.35f;
    static constexpr float GRAB_MAX_SPEED = 40.0f;
    static constexpr float GRAB_MIN_DISTANCE = 0.5f;
    static constexpr float GRAB_WHEEL_STEP = 1.1f;
    static constexpr float GRAB_MARKER_PX = 7.0f;
    static constexpr float GRAB_SPHERE_PX = 10.0f;

    struct PhysicsGrab
    {
        bool active = false;
        bool hover = false;
        uint64_t key = 0;
        PhysicsBodyId body;
        vec3 local_anchor = vec3(0.0f);
        vec3 anchor = vec3(0.0f);
        vec3 target = vec3(0.0f);
        vec3 hover_point = vec3(0.0f);
        float distance = 0.0f;
    } grab;

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
    void _spawn_at_mouse(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size);
    vec3 _mouse_ray(const EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size) const;
    bool _grab_body(EditorContext& ctx, uint64_t p_key, PhysicsBodyId& r_body) const;
    void _physics_grab(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size, bool p_hovered, bool p_clicked);
    void _draw_grab(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size) const;
    void _draw_cull_frustum(EditorContext& ctx);
    bool _draw_gizmo(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size, bool p_hover_ok);
    static void draw_bar(ImVec2 p_min, ImVec2 p_max, bool p_overlay = true);
    static void draw_stats(EditorContext& ctx, ImVec2 p_pos);
    void _draw_image(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size);
    void _asset_drop(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size);

    static void draw_options_menu(EditorContext& ctx);
    void draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
};

}
