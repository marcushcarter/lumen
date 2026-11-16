#include <editor/viewport/viewport.h>
#include <drivers/imgui/imgui_driver.h>
#include <editor/ui/toolbar_row.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/ui_style.h>
#include <editor/layout/panel.h>
#include <core/application/frame_stats.h>
#include <core/rendering/renderer.h>
#include <core/base/profiling.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/rendering/features/editor/debug_view.h>
#include <core/scene/scene.h>
#include <core/scene/components.h>
#include <core/scene/template.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/documents/scene/editor_selection.h>
#include <editor/documents/scene/editor_spawn.h>
#include <editor/documents/scene/template_document.h>
#include <editor/play/editor_play.h>
#include <core/rendering/debug_draw.h>
#include <core/animation/bone_debug.h>
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cmath>
#include <iterator>

namespace lumen {

static constexpr const char* VIEW_OVERLAY_NAMES[] = { "None", "Shape Collisions", "All Collisions", "Debug Lines", "Bones", "Everything" };
static_assert(std::size(VIEW_OVERLAY_NAMES) == (size_t)ViewOverlay::COUNT);
static constexpr uint32_t EDITOR_LINE_MASK = (uint32_t)DebugCategory::GENERAL | (uint32_t)DebugCategory::COLLISION | (uint32_t)DebugCategory::BONES;

ViewportView& viewport_view()
{
    static ViewportView view;
    return view;
}

static void _outline_rect(EditorContext& ctx, OutlineFeature& r_outline, const Scene& p_world, const std::vector<Entity>& p_entities)
{
    static constexpr size_t MAX_BOUNDED = 4096;
    const Scene& world = p_world;
    const Renderer& renderer = *ctx.renderer;
    if (p_entities.size() > MAX_BOUNDED || renderer.main_view.output_width == 0 || renderer.main_view.output_height == 0) {
        r_outline.set_rect(vec2(0.0f), vec2(1.0f));
        return;
    }
    const Camera& cam = renderer.main_view.camera;
    const mat4 view_proj = cam.view_proj((float)renderer.main_view.output_width / (float)renderer.main_view.output_height);
    vec2 lo(FLT_MAX);
    vec2 hi(-FLT_MAX);
    bool full = false;

    auto add_box = [&](const mat4& p_mvp, vec3 p_min, vec3 p_max) {
        vec2 box_lo(FLT_MAX);
        vec2 box_hi(-FLT_MAX);
        uint32_t behind = 0;
        for (uint32_t c = 0; c < 8; c++) {
            const vec3 corner((c & 1) ? p_max.x : p_min.x, (c & 2) ? p_max.y : p_min.y, (c & 4) ? p_max.z : p_min.z);
            const vec4 clip = p_mvp * vec4(corner, 1.0f);
            if (clip.w <= cam.near_z) {
                behind++;
                continue;
            }
            const vec2 uv = vec2(clip) / clip.w * 0.5f + 0.5f;
            box_lo = min(box_lo, uv);
            box_hi = max(box_hi, uv);
        }
        if (behind == 8) return;
        if (behind > 0) {
            full = true;
            return;
        }
        lo = min(lo, box_lo);
        hi = max(hi, box_hi);
    };

    for (const Entity e : p_entities) {
        const TransformComponent* xf = world.try_get<TransformComponent>(e);
        if (!xf || world.has<DisabledTag>(e)) continue;
        const mat4 mvp = view_proj * world.world_matrix(e);
        if (const ModelComponent* m = world.try_get<ModelComponent>(e)) {
            const uint32_t mesh_index = renderer.models.mesh_index(m->model);
            const PoseComponent* pose = world.has<StaticTag>(e) ? nullptr : world.try_get<PoseComponent>(e);
            vec3 box_min, box_max;
            renderer.geometry.posed_box(mesh_index, pose ? pose->skin.data() : nullptr, pose ? (uint32_t)pose->skin.size() : 0u, box_min, box_max);
            if (renderer.geometry.get(mesh_index)) add_box(mvp, box_min, box_max);
        }
        if (full) break;
    }
    if (full) r_outline.set_rect(vec2(0.0f), vec2(1.0f));
    else r_outline.set_rect(lo, hi);
}

bool Viewport::_draw_gizmo(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size, bool p_hover_ok)
{
    if (!ctx.scene || !ctx.selection || p_size.x < 2.0f || p_size.y < 2.0f) {
        gizmo.reset();
        return false;
    }
    const Entity selected = ctx.selection->primary();
    if (!ctx.scene->has<TransformComponent>(selected)) {
        gizmo.reset();
        shape_gizmo.reset();
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const TransformGizmo::Frame frame = ShapeGizmo::frame(ctx.renderer->main_view.camera, p_pos, p_size);
    const bool input_ok = ImGui::IsMousePosValid() && !io.AppFocusLost && !io.WantTextInput;

    CollisionComponent* collision = ctx.scene->try_get<CollisionComponent>(selected);
    const int32_t shape = ctx.selection->shape();
    if (collision && collision->source == CollisionSource::SHAPES && shape >= 0 && shape < (int32_t)std::min(collision->shape_count, COLLISION_MAX_SHAPES)) {
        gizmo.reset();
        CollisionShapeEntry& s = collision->shapes[shape];
        const ShapeEdit edit{ .kind = s.type, .position = &s.position, .rotation = &s.rotation, .half_extents = &s.half_extents, .radius = &s.radius, .half_height = &s.half_height, .top_radius = &s.top_radius };
        bool shape_changed = false;
        const bool shape_hot = shape_gizmo.update(frame, ctx.scene->world_matrix(selected), gizmo_world, p_hover_ok && input_ok, edit, shape_changed);
        if (shape_changed) ctx.scene->touch(selected);
        return shape_hot;
    }
    shape_gizmo.reset();
    TransformComponent xf = ctx.scene->world_transform(selected);
    bool changed = false;
    const bool hot = gizmo.update(frame, gizmo_world, p_hover_ok && input_ok, xf, changed);
    if (changed) ctx.scene->set_world_matrix(selected, transform_matrix(xf));
    return hot;
}

const char* view_overlay_name(ViewOverlay p_overlay)
{
    return VIEW_OVERLAY_NAMES[std::min((size_t)p_overlay, std::size(VIEW_OVERLAY_NAMES) - 1)];
}

uint32_t view_overlay_kinds(ViewOverlay p_overlay)
{
    switch (p_overlay) {
        case ViewOverlay::SHAPE_COLLISIONS: return ENTITY_DEBUG_COLLISION;
        case ViewOverlay::ALL_COLLISIONS: return ENTITY_DEBUG_COLLISION | ENTITY_DEBUG_MESH_COLLISION;
        case ViewOverlay::BONES: return ENTITY_DEBUG_BONES;
        case ViewOverlay::EVERYTHING: return ENTITY_DEBUG_COLLISION | ENTITY_DEBUG_MESH_COLLISION | ENTITY_DEBUG_BONES;
        default: return 0;
    }
}

uint32_t view_selection_kinds(ViewOverlay p_overlay)
{
    return ENTITY_DEBUG_COLLISION | ENTITY_DEBUG_BONES | (view_overlay_kinds(p_overlay) & ENTITY_DEBUG_MESH_COLLISION);
}

bool view_overlay_lines(ViewOverlay p_overlay)
{
    return p_overlay == ViewOverlay::DEBUG_LINES || p_overlay == ViewOverlay::EVERYTHING;
}

void Viewport::_submit_debug_draw(EditorContext& ctx, bool p_live)
{
    const ViewportView& view = viewport_view();
    if (view.culling_paused) _draw_cull_frustum(ctx);
    debug_entities.clear();
    if (!ctx.renderer) return;
    const Scene* scene = p_live ? &play->runtime : ctx.scene;
    if (!scene) return;

    if (ctx.selection) {
        std::vector<Entity> sub;
        for (const Entity e : ctx.selection->entities) {
            const Entity root = p_live ? play->to_runtime(e) : e;
            scene->subtree(root, sub);
            for (const Entity s : sub) if (std::find(debug_entities.begin(), debug_entities.end(), s) == debug_entities.end()) debug_entities.push_back(s);
        }
    }
    const EntityDebugSource source{ .renderer = *ctx.renderer, .project = ctx.project, .meshes = &collision_meshes };
    const uint32_t all = view_overlay_kinds(view.overlay);
    debug_draw_scene(debug_draw(), *scene, source, all);
    debug_draw_entities(debug_draw(), *scene, debug_entities, source, view_selection_kinds(view.overlay) & ~all);
}

void Viewport::_draw_cull_frustum(EditorContext& ctx)
{
    if (!ctx.render_path || !ctx.render_path->geometry.frozen) return;
    const CameraUniform& cam = ctx.render_path->geometry.frozen_camera;
    const vec3 eye = vec3(cam.position);
    const float t = CULL_FRUSTUM_DISTANCE / cam.far_z;
    vec3 near_pts[4];
    vec3 far_pts[4];
    static constexpr vec2 CORNERS[4] = { vec2(-1.0f, -1.0f), vec2(1.0f, -1.0f), vec2(1.0f, 1.0f), vec2(-1.0f, 1.0f) };
    for (uint32_t i = 0; i < 4; i++) {
        const vec4 n = cam.inv_view_proj * vec4(CORNERS[i], 1.0f, 1.0f);
        const vec4 f = cam.inv_view_proj * vec4(CORNERS[i], 0.0f, 1.0f);
        near_pts[i] = vec3(n) / n.w;
        far_pts[i] = eye + (vec3(f) / f.w - eye) * t;
    }
    DebugDraw& draw = debug_draw();
    const DebugDraw::Style style{ .color = DebugDraw::YELLOW };
    for (uint32_t i = 0; i < 4; i++) {
        const uint32_t j = (i + 1) % 4;
        draw.line(near_pts[i], near_pts[j], style);
        draw.line(far_pts[i], far_pts[j], style);
        draw.line(eye, far_pts[i], style);
    }
    draw.point(eye, 4.0f, style);
}

struct ViewBar
{
    float x = 0.0f;
    float y = 0.0f;
    float h = 0.0f;

    bool combo(const char* p_id, const char* p_preview, float p_width)
    {
        const UiStyle& ui = ui_style();
        const ImVec2 a(x, y);
        const ImVec2 b(x + p_width, y + h);
        x = b.x + 1.0f;
        ImGui::SetCursorScreenPos(a);
        const ImGuiID popup = ImGui::GetID(p_id);
        const bool open_now = ImGui::IsPopupOpen(popup, ImGuiPopupFlags_None);
        if (ImGui::InvisibleButton(p_id, ImVec2(p_width, h), ImGuiButtonFlags_PressedOnClick) && !open_now) ImGui::OpenPopupEx(popup);
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, (hovered || open_now) ? ui.color.field_hover : ui.color.field, ui.radius.sm);
        const float pad = ui.space.sm;
        const float arrow = ImFloor(ImGui::GetFontSize() * 0.32f);
        const ImVec2 ts = ImGui::CalcTextSize(p_preview);
        const ImVec2 tmin(a.x + pad, ImFloor(a.y + (h - ts.y) * 0.5f));
        const ImVec2 tmax(b.x - pad - arrow * 3.0f, b.y);
        ImGui::PushStyleColor(ImGuiCol_Text, ui.color.text);
        ImGui::RenderTextEllipsis(dl, tmin, tmax, tmax.x, p_preview, nullptr, &ts);
        ImGui::PopStyleColor();
        ui_chevron(dl, ImVec2(ImFloor(b.x - pad - arrow) + 0.5f, ImFloor(a.y + h * 0.5f) + 0.5f), arrow * 2.0f, ImGuiDir_Down, ui.color.text, ui.stroke.thin);
        ImGui::SetNextWindowPos(ImVec2(a.x, b.y), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(p_width, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
        ui_dropdown_push_style();
        const bool open = ImGui::BeginPopupEx(popup, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings);
        if (!open) ui_dropdown_pop_style();
        return open;
    }

    void end_combo()
    {
        ImGui::EndPopup();
        ui_dropdown_pop_style();
    }

    bool toggle(const char* p_id, const char* p_label, const char* p_tip, bool p_on)
    {
        const UiStyle& ui = ui_style();
        const float w = ImMax(h, ImGui::CalcTextSize(p_label).x + ui.space.sm * 2.0f);
        const ImVec2 a(x, y);
        const ImVec2 b(x + w, y + h);
        x = b.x + 1.0f;
        ImGui::SetCursorScreenPos(a);
        const bool pressed = ImGui::InvisibleButton(p_id, ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        if (p_tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", p_tip);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (p_on) dl->AddRectFilled(a, b, ui.color.accent, ui.radius.sm);
        else if (hovered) {
            dl->AddRectFilled(a, b, ui.color.tool_hover, ui.radius.sm);
            dl->AddRect(a, b, ui.color.accent, ui.radius.sm);
        }
        ui_text_centered(dl, a, b, p_on ? ui.color.active_icon : ui.color.text, p_label);
        return pressed;
    }
};

void Viewport::draw_bar(ImVec2 p_min, ImVec2 p_max)
{
    ViewportView& view = viewport_view();
    ImGui::SetCursorScreenPos(p_min);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##viewport_bar", ImVec2(p_max.x - p_min.x, p_max.y - p_min.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();

    ViewBar bar;
    bar.h = p_max.y - p_min.y;
    bar.x = p_min.x;
    bar.y = p_min.y;
    const float combo_w = ImFloor(ImGui::GetFontSize() * 7.0f);

    const int selected_view = std::clamp(view.debug_view, 0, DEBUG_VIEW_COUNT - 1);
    if (bar.combo("##render_view", DEBUG_VIEWS[selected_view].name, combo_w * 1.4f)) {
        int i = 0;
        while (i < DEBUG_VIEW_COUNT) {
            const DebugView& d = DEBUG_VIEWS[i];
            if (!d.category) {
                if (ImGui::MenuItem(d.name, nullptr, i == selected_view)) view.debug_view = i;
                i++;
                continue;
            }
            int j = i;
            while (j < DEBUG_VIEW_COUNT && DEBUG_VIEWS[j].category == d.category) j++;
            if (ImGui::BeginMenu(d.category->name)) {
                for (int k = i; k < j; k++) {
                    if (ImGui::MenuItem(DEBUG_VIEWS[k].name, nullptr, k == selected_view)) view.debug_view = k;
                }
                ImGui::EndMenu();
            }
            i = j;
        }
        bar.end_combo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Render buffer");

    char overlay_label[64];
    std::snprintf(overlay_label, sizeof(overlay_label), "Overlay: %s", view_overlay_name(view.overlay));
    if (bar.combo("##overlay", overlay_label, combo_w * 1.8f)) {
        for (int i = 0; i < (int)ViewOverlay::COUNT; i++) {
            if (ImGui::MenuItem(VIEW_OVERLAY_NAMES[i], nullptr, (int)view.overlay == i)) view.overlay = (ViewOverlay)i;
        }
        bar.end_combo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Debug drawn for every entity. The selection always shows its own.");

    if (bar.toggle("##freeze_culling", ICON_FA_SNOWFLAKE "  Freeze Culling", view.culling_paused ? "Culling is frozen where you paused it" : "Freeze culling", view.culling_paused)) view.culling_paused = !view.culling_paused;
    if (bar.toggle("##stats", "IM", "Frame stats", view.stats)) view.stats = !view.stats;
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::EndChild();
}

void Viewport::draw_stats(EditorContext& ctx, ImVec2 p_pos)
{
    const UiStyle& ui = ui_style();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const RenderView& v = ctx.renderer->main_view;
    const float ms = ctx.frame_stats ? ctx.frame_stats->frame_avg : 0.0f;
    const TerrainCache& terrain = ctx.renderer->terrain;
    const uint32_t patches = terrain.owner == &v.frame && terrain.frame_active ? terrain.patch_count : 0u;
    const uint64_t terrain_tris = (uint64_t)patches * TERRAIN_QUARTER_QUADS * TERRAIN_QUARTER_QUADS * 2u;
    uint64_t mesh_tris = 0;
    uint32_t clusters = 0;
    if (ctx.render_path) {
        const GeometryFeature& geo = ctx.render_path->geometry;
        if (geo.compact_raster_frame) mesh_tris = geo.compact_demand;
        clusters = geo.stats.phase1_visible + geo.stats.phase2_visible;
    }
    char line0[256];
    const float cpu_ms = ctx.frame_stats ? std::max(ctx.frame_stats->frame_avg - ctx.frame_stats->wait_avg, 0.0f) : 0.0f;
    std::snprintf(line0, sizeof(line0), "FPS Main: %.1f Render: %.1f (ms: %.2f, %.2f) - Instances: %u - Triangles: %llu - Clusters: %u - Patches: %u", ms > 0.0f ? 1000.0f / ms : 0.0f, cpu_ms > 0.0f ? 1000.0f / cpu_ms : 0.0f, ms, cpu_ms, v.frame.instance_count + patches, (unsigned long long)(mesh_tris + terrain_tris), clusters, patches);

    drivers::DeviceDriverVulkan& dd = *ctx.renderer->dd;
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
    vmaGetHeapBudgets(dd.allocator, budgets);
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(dd.physical_device, &mem);
    uint64_t used = 0;
    uint64_t total = 0;
    for (uint32_t h = 0; h < mem.memoryHeapCount; h++) {
        if ((mem.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) continue;
        used += budgets[h].usage;
        total += mem.memoryHeaps[h].size;
    }
    constexpr double MB = 1024.0 * 1024.0;
    const VkPhysicalDeviceProperties& props = dd.physical_device_properties;
    char line1[320];
    std::snprintf(line1, sizeof(line1), "Vulkan %u.%u.%u.%u (%s, [%u x %u], VRAM %.1f%% %.0fMB/%.0fMB)", VK_API_VERSION_MAJOR(props.apiVersion), VK_API_VERSION_MINOR(props.apiVersion), VK_API_VERSION_PATCH(props.apiVersion), VK_API_VERSION_VARIANT(props.apiVersion), props.deviceName, v.width, v.height, total ? 100.0 * (double)used / (double)total : 0.0, (double)used / MB, (double)total / MB);
    const float lh = ImGui::GetTextLineHeight();
    const ImVec2 at(p_pos.x + ui.space.sm, p_pos.y + ui.space.xs);
    const char* lines[2] = { line0, line1 };
    for (int i = 0; i < 2; i++) {
        const ImVec2 p(at.x, at.y + lh * (float)i);
        dl->AddText(ImVec2(p.x + 1.0f, p.y + 1.0f), ui.color.shadow_text, lines[i]);
        dl->AddText(p, ui.color.stats_text, lines[i]);
    }
}

void Viewport::_draw_image(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size)
{
    const UiStyle& ui = ui_style();
    const ViewportView& view = viewport_view();
    const bool live = play && play->active();
    const bool playing = play && play->playing();

    uint32_t picked = PickFeature::NONE;
    float picked_depth = 0.0f;
    const bool pick_ready = ctx.render_path && ctx.scene && ctx.selection && ctx.render_path->pick.take_result(picked, picked_depth);
    if (drop.active && (pick_ready || ++drop.frames > DROP_TIMEOUT_FRAMES)) _finish_drop(ctx, picked_depth, pick_ready);
    else if (pick_ready) {
        Scene& world = *ctx.scene;
        Entity e = picked != PickFeature::NONE ? (live ? play->to_authored(picked) : world.entity_at(picked)) : ENTITY_NULL;
        const Entity root = template_root_of(world, e);
        const Entity primary = ctx.selection->primary();
        if (root != ENTITY_NULL && primary != e && primary != root && template_root_of(world, primary) != root) e = root;
        ctx.selection->clear();
        if (world.has<EntityIdComponent>(e)) ctx.selection->select(e);
    }

    const bool outline_on = !playing;
    if (ctx.render_path) ctx.render_path->outline.enabled = outline_on;
    if (outline_on && ctx.render_path && ctx.selection) {
        if (live) {
            _view_selection.clear();
            for (const Entity e : ctx.selection->entities) {
                const Entity r = play->to_runtime(e);
                if (r != ENTITY_NULL) _view_selection.push_back(r);
            }
            ctx.render_path->outline.set_selection(_view_selection.data(), (uint32_t)_view_selection.size());
            ctx.render_path->outline.source_version = 0;
        } else if (ctx.render_path->outline.source_version != ctx.selection->version) {
            ctx.render_path->outline.set_selection(ctx.selection->entities.data(), (uint32_t)ctx.selection->entities.size());
            ctx.render_path->outline.source_version = ctx.selection->version;
        }
    }
    if (outline_on && ctx.render_path && ctx.scene && ctx.selection && !ctx.selection->entities.empty()) {
        if (live) _outline_rect(ctx, ctx.render_path->outline, play->runtime, _view_selection);
        else _outline_rect(ctx, ctx.render_path->outline, *ctx.scene, ctx.selection->entities);
    }

    const uint32_t w = (uint32_t)ImClamp(p_size.x, 1.0f, 8192.0f);
    const uint32_t h = (uint32_t)ImClamp(p_size.y, 1.0f, 8192.0f);
    ctx.renderer->request_size(w, h);

    if (ctx.render_path) {
        GridFeature& grid = ctx.render_path->grid;
        grid.show_grid = !playing;
        grid.show_axes = !playing;
        const ImVec4 gc = ui_vec4(ui.color.grid);
        const ImVec4 xc = ui_vec4(ui.color.axis_x);
        const ImVec4 zc = ui_vec4(ui.color.axis_z);
        grid.grid_color = vec4(gc.x, gc.y, gc.z, gc.w);
        grid.axis_x_color = vec4(xc.x, xc.y, xc.z, xc.w);
        grid.axis_z_color = vec4(zc.x, zc.y, zc.z, zc.w);
        ctx.render_path->lines.mask = EDITOR_LINE_MASK;
        _submit_debug_draw(ctx, live);
    }

    const char* vp_image = ctx.render_path ? ctx.render_path->ui.viewport_image(ctx.renderer->main_view.graph) : nullptr;
    RenderGraph::ImageResource* sel = vp_image ? ctx.renderer->main_view.graph.image_resource(vp_image) : nullptr;
    VkImageView sel_view = VK_NULL_HANDLE;
    if (sel && sel->image && sel->image->state.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) sel_view = sel->image->image_view;
    const VkDescriptorSet set = ctx.imgui->texture_cache.get(sel_view);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p_end(p_pos.x + p_size.x, p_pos.y + p_size.y);
    ImGui::SetCursorScreenPos(p_pos);
    ImGui::InvisibleButton("##scene_image", p_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool image_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool image_hovered = ImGui::IsItemHovered();
    const bool fly_click = !playing && ImGui::IsItemClicked(ImGuiMouseButton_Right);
    if (set) dl->AddImage((ImTextureID)set, p_pos, p_end, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    else dl->AddRectFilled(p_pos, p_end, ui.color.viewport_back);

    const bool gizmo_hover_ok = image_hovered && !popup_open_last_frame && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    const bool gizmo_hot = !playing && set && _draw_gizmo(ctx, p_pos, p_size, gizmo_hover_ok);
    if (playing) {
        gizmo.reset();
        shape_gizmo.reset();
    }
    const bool pick_click = !playing && !gizmo_hot && image_clicked;
    const bool game_click = playing && image_clicked;
    const ImVec2 mouse = ImGui::GetMousePos();
    if (!playing) _asset_drop(ctx, p_pos, p_size);
    if (view.stats) draw_stats(ctx, p_pos);

    if (pick_click && ctx.render_path && !popup_open_last_frame) ctx.render_path->pick.request((mouse.x - p_pos.x) / p_size.x, 1.0f - (mouse.y - p_pos.y) / p_size.y);
    if (game_click && !game_captured && !popup_open_last_frame) {
        game_captured = true;
        cursor_lock.begin();
    }
    if (game_captured && playing && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        game_captured = false;
        cursor_lock.end();
        if (ctx.pie_toggle_pause) ctx.pie_toggle_pause();
    }
    if (game_captured && !playing) {
        game_captured = false;
        cursor_lock.end();
    }
    if (game_captured) {
        float dx = 0.0f;
        float dy = 0.0f;
        cursor_lock.take_delta(dx, dy);
        game_look.x += dx;
        game_look.y += dy;
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);
    }
    _update_fly(fly_click && gizmo.active == TransformGizmo::Handle::NONE && shape_gizmo.gizmo.active == TransformGizmo::Handle::NONE && !popup_open_last_frame);
    popup_open_last_frame = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);

    if (ctx.render_path) {
        ctx.render_path->geometry.freeze = view.culling_paused;
        ctx.render_path->geometry.collect_stats = view.stats;
        ctx.renderer->terrain.freeze = view.culling_paused;
        ctx.render_path->debug.view = (uint32_t)std::clamp(view.debug_view, 0, DEBUG_VIEW_COUNT - 1);
    }
}

void Viewport::_asset_drop(EditorContext& ctx, ImVec2 p_pos, ImVec2 p_size)
{
    if (!ctx.scene || !ctx.selection) return;
    if (!ImGui::BeginDragDropTargetCustom(ImRect(p_pos, ImVec2(p_pos.x + p_size.x, p_pos.y + p_size.y)), ImGui::GetID("##asset_drop"))) return;
    if (const ImGuiPayload* peek = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE, ImGuiDragDropFlags_AcceptPeekOnly)) {
        const AssetDragPayload& asset = *(const AssetDragPayload*)peek->Data;
        const bool template_ok = asset.type == AssetType::TEMPLATE && !ctx.template_doc;
        if ((asset.type == AssetType::MODEL || template_ok) && asset.guid.valid() && ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
            const Camera& cam = ctx.renderer->main_view.camera;
            const ImVec2 mouse = ImGui::GetMousePos();
            const float u = std::clamp((mouse.x - p_pos.x) / p_size.x, 0.0f, 1.0f);
            const float v = std::clamp((mouse.y - p_pos.y) / p_size.y, 0.0f, 1.0f);
            const float tan_half = std::tan(cam.fov_y * 0.5f);
            const float aspect = p_size.x / std::max(p_size.y, 1.0f);
            drop.active = true;
            drop.is_template = asset.type == AssetType::TEMPLATE;
            drop.guid = asset.guid;
            drop.origin = cam.position;
            drop.direction = normalize(cam.forward() + cam.right() * ((u * 2.0f - 1.0f) * tan_half * aspect) + cam.up() * ((1.0f - v * 2.0f) * tan_half));
            drop.frames = 0;
            if (ctx.render_path) ctx.render_path->pick.request(u, 1.0f - v);
        }
    }
    ImGui::EndDragDropTarget();
}

void Viewport::_finish_drop(EditorContext& ctx, float p_depth, bool p_hit)
{
    drop.active = false;
    if (!ctx.scene || !ctx.selection) return;
    const Camera& cam = ctx.renderer->main_view.camera;
    float distance = SPAWN_DISTANCE;
    if (p_hit && p_depth > 0.0f) {
        const float view_z = (cam.near_z * cam.far_z) / (p_depth * (cam.far_z - cam.near_z) + cam.near_z);
        distance = view_z / std::max(dot(drop.direction, cam.forward()), 1e-4f);
    }
    const vec3 position = drop.origin + drop.direction * distance;
    if (drop.is_template) editor_spawn_template(ctx, drop.guid, ENTITY_NULL, &position);
    else editor_spawn(ctx, SpawnType::MODEL, ENTITY_NULL, drop.guid, &position);
}

void Viewport::_update_fly(bool p_start)
{
    if (!flying && p_start && !game_captured) {
        flying = true;
        cursor_lock.begin();
    }
    if (!flying) return;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) || (play && play->playing())) {
        flying = false;
        cursor_lock.end();
        return;
    }
    float dx = 0.0f;
    float dy = 0.0f;
    cursor_lock.take_delta(dx, dy);
    fly_look.x += dx;
    fly_look.y += dy;
    fly_wheel += ImGui::GetIO().MouseWheel;
    ImGui::SetMouseCursor(ImGuiMouseCursor_None);
}

void Viewport::draw_options_menu(EditorContext& ctx)
{
    ui_menu_push_style();
    if (ImGui::BeginPopup("##viewport_menu")) {
        ImGui::PushItemWidth(ImFloor(ImGui::GetFontSize() * 9.0f));
        float internal = ctx.renderer->main_view.render_scale * 100.0f;
        if (ImGui::SliderFloat("Internal Resolution", &internal, RenderView::MIN_RENDER_SCALE * 100.0f, RenderView::MAX_RENDER_SCALE * 100.0f, "%.0f%%")) ctx.renderer->main_view.render_scale = internal / 100.0f;
        float lod_bias = ctx.renderer->main_view.lod_bias * 100.0f;
        if (ImGui::SliderFloat("LOD Bias", &lod_bias, 25.0f, 400.0f, "%.0f%%")) ctx.renderer->main_view.lod_bias = lod_bias / 100.0f;
        ImGui::PopItemWidth();
        ImGui::EndPopup();
    }
    ui_menu_pop_style();
}

void Viewport::draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    const float bar_h = ui.row;
    if (p_max.y - p_min.y < bar_h + 2.0f || p_max.x - p_min.x < 2.0f) return;

    const ImVec2 bar_min = p_min;
    const ImVec2 bar_max(p_max.x, bar_min.y + bar_h);
    ImGui::GetWindowDrawList()->AddRectFilled(bar_min, bar_max, ui.color.view_bar);
    draw_bar(bar_min, bar_max);

    const bool live = play && play->active();
    const float border = live ? ui.viewport.play_border : 0.0f;
    const ImVec2 img_min(p_min.x + border, bar_max.y + border);
    const ImVec2 img_max(p_max.x - border, p_max.y - border);
    _draw_image(ctx, img_min, ImVec2(ImMax(1.0f, img_max.x - img_min.x), ImMax(1.0f, img_max.y - img_min.y)));
    if (live) ImGui::GetWindowDrawList()->AddRect(ImVec2(p_min.x + border * 0.5f, bar_max.y + border * 0.5f), ImVec2(p_max.x - border * 0.5f, p_max.y - border * 0.5f), ui.color.play_border, 0.0f, 0, border);
}

}
