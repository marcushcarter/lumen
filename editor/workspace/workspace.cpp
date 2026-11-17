#include <editor/workspace/workspace.h>
#include <core/rendering/renderer.h>
#include <editor/documents/document_manager.h>
#include <editor/layout/document_layout.h>
#include <editor/tools/settings/editor_settings_tool.h>
#include <editor/tools/settings/project_settings_tool.h>
#include <editor/tools/version_control/version_control_tool.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <iterator>
#include <vector>

namespace lumen {

static constexpr const char* TRANSFORM_ORIENTATION_NAMES[] = { "Global", "Local" };

void Workspace::initialize()
{
    layout.add(&content, DockZone::RIGHT_TOP);
    version_control = std::make_unique<ToolPanel>(std::make_unique<VersionControlTool>(), "Version Control", ICON_FA_CODE_BRANCH);
    editor_settings = std::make_unique<ToolPanel>(std::make_unique<EditorSettingsTool>(), "Editor Settings", ICON_FA_GEAR);
    project_settings = std::make_unique<ToolPanel>(std::make_unique<ProjectSettingsTool>(), "Project Settings", ICON_FA_WRENCH);
    layout.add(&gpu_profiler, DockZone::BOTTOM, false);
    layout.add(&memory, DockZone::BOTTOM, false);
    layout.add(version_control.get(), DockZone::RIGHT_BOTTOM, false);
    layout.add(editor_settings.get(), DockZone::RIGHT_BOTTOM, false);
    layout.add(project_settings.get(), DockZone::RIGHT_BOTTOM, false);
}

void Workspace::_release_tools(EditorContext& ctx)
{
    version_control->release(ctx);
    editor_settings->release(ctx);
    project_settings->release(ctx);
    active_document = nullptr;
}

void Workspace::shutdown(EditorContext& ctx)
{
    _release_tools(ctx);
    scene.shutdown(ctx);
    content.shutdown();
    gpu_profiler.shutdown();
    memory.shutdown();
}

void Workspace::reset(EditorContext& ctx)
{
    _release_tools(ctx);
    scene.reset(ctx);
}

std::string Workspace::document_name() const
{
    if (active_document) {
        if (const std::filesystem::path* p = active_document->asset_path()) return p->filename().string();
        return active_document->title();
    }
    return scene.document_name();
}

bool Workspace::dirty() const
{
    return active_document ? active_document->dirty() : scene.dirty();
}

Error Workspace::save(EditorContext& ctx)
{
    if (active_document) return active_document->locked(ctx) ? Error::OK : active_document->save(ctx);
    return scene.save(ctx);
}

void Workspace::tick(EditorContext& ctx)
{
    _sync_documents(ctx);
    scene.tick(ctx);
}

void Workspace::_sync_documents(EditorContext& ctx)
{
    if (!ctx.documents) return;
    bool found = false;
    for (auto& d : ctx.documents->documents) {
        if (d->_focus_request) {
            d->_focus_request = false;
            active_document = d.get();
        }
        found |= d.get() == active_document;
    }
    if (!found) active_document = nullptr;
}

void Workspace::_sync_document_panels(EditorContext& ctx)
{
    std::vector<Panel*> panels;
    bool locked = false;
    if (DocumentLayout* doc = active_document ? active_document->panels() : nullptr) {
        for (auto& p : doc->panels) panels.push_back(p.get());
        locked = active_document->locked(ctx);
    } else if (!active_document) {
        panels = { &scene.scene_panel, &scene.properties };
    }
    const DockZone side[2] = { layout.document_zone(&scene.scene_panel, DockZone::LEFT_TOP), layout.document_zone(&scene.properties, DockZone::LEFT_BOTTOM) };
    std::vector<EditorLayout::Slot> slots;
    uint32_t side_count = 0;
    for (Panel* p : panels) {
        const DockZone fallback = p->slot() == PanelSlot::BOTTOM ? DockZone::BOTTOM : side[std::min(side_count++, 1u)];
        slots.push_back({ p, layout.document_zone(p, fallback), true });
    }
    layout.set_document(slots, locked);
}

struct ToolGrid
{
    ImVec2 origin;
    float cell = 0.0f;
    float gap = 0.0f;
    float x = 0.0f;

    bool _button(const char* p_id, const char* p_icon, const char* p_tip, ImVec2 p_a, ImVec2 p_b, bool p_active, bool p_enabled, float p_icon_size)
    {
        const UiStyle& ui = ui_style();
        ImGui::SetCursorScreenPos(p_a);
        const bool pressed = ImGui::InvisibleButton(p_id, ImVec2(p_b.x - p_a.x, p_b.y - p_a.y));
        const bool hovered = p_enabled && ImGui::IsItemHovered();
        const bool held = p_enabled && ImGui::IsItemActive();
        if (p_tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", p_tip);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float r = ui.radius.sm;
        if (p_active || held) dl->AddRectFilled(p_a, p_b, ui.color.accent, r);
        else if (hovered) {
            dl->AddRectFilled(p_a, p_b, ui.color.tool_hover, r);
            dl->AddRect(p_a, p_b, ui.color.accent, r);
        }
        ImU32 col = (p_active || held) ? ui.color.active_icon : ui.color.text;
        if (!p_enabled) col = ui_with_alpha(ui.color.text_dim, ui.alpha.disabled);
        ImFont* font = ImGui::GetFont();
        const ImVec2 ts = font->CalcTextSizeA(p_icon_size, FLT_MAX, 0.0f, p_icon);
        dl->AddText(font, p_icon_size, ImVec2(ImFloor((p_a.x + p_b.x - ts.x) * 0.5f), ImFloor((p_a.y + p_b.y - ts.y) * 0.5f)), col, p_icon);
        return pressed && p_enabled;
    }

    bool big(const char* p_id, const char* p_icon, const char* p_tip, bool p_active, bool p_enabled)
    {
        const float s = cell * 2.0f;
        const ImVec2 a(x, origin.y);
        x += s;
        return _button(p_id, p_icon, p_tip, a, ImVec2(a.x + s, a.y + s), p_active, p_enabled, ImGui::GetFontSize() * 1.7f);
    }

    bool small(int p_row, const char* p_id, const char* p_icon, const char* p_tip, bool p_active, bool p_enabled)
    {
        const ImVec2 a(x, origin.y + cell * (float)p_row);
        return _button(p_id, p_icon, p_tip, a, ImVec2(a.x + cell, a.y + cell), p_active, p_enabled, ImGui::GetFontSize());
    }

    ImVec2 slot(int p_row) const { return ImVec2(x, origin.y + cell * (float)p_row); }
    void column(float p_width) { x += p_width + gap; }

    void separator()
    {
        x += ui_style().space.lg;
    }
};

void Workspace::_draw_toolbar(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    ImGui::SetCursorScreenPos(p_min);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##main_toolbar", ImVec2(p_max.x - p_min.x, p_max.y - p_min.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p_min, p_max, ui.color.toolbar);

    const bool playing = ctx.pie_is_playing && ctx.pie_is_playing();
    const bool paused = playing && ctx.pie_is_paused && ctx.pie_is_paused();
    const bool can_play = scene.can_play() && ctx.pie_toggle_play;

    ToolGrid g;
    g.origin = p_min;
    g.gap = 0.0f;
    g.cell = ui.row;
    g.x = p_min.x;

    if (g.small(0, "##save", ICON_FA_FLOPPY_DISK, "Save (Ctrl+S)", false, dirty())) save(ctx);
    if (g.small(1, "##save_all", ICON_FA_FOLDER_CLOSED, "Save All (Ctrl+Shift+S)", false, true) && ctx.save_all_callback) ctx.save_all_callback();
    g.column(g.cell);
    g.separator();

    if (!playing) {
        if (g.big("##play", ICON_FA_PLAY, can_play ? "Play" : "Open a scene to play", false, can_play)) ctx.pie_toggle_play();
    } else {
        if (g.big("##stop", ICON_FA_STOP, "Stop", true, true)) ctx.pie_toggle_play();
    }
    if (g.small(0, "##pause", paused ? ICON_FA_PLAY : ICON_FA_PAUSE, paused ? "Resume" : "Pause and edit", paused, playing && (bool)ctx.pie_toggle_pause)) ctx.pie_toggle_pause();
    if (g.small(1, "##step", ICON_FA_FORWARD_STEP, "Step one frame", false, paused && (bool)ctx.pie_step)) ctx.pie_step();
    g.column(g.cell);
    g.separator();

    const float field_w = ImFloor(ImGui::CalcTextSize("Global").x * 3.0f);
    const int orientation = scene.viewport.gizmo_world ? 0 : 1;
    const float field_h = g.cell;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, ImFloor((field_h - ImGui::GetFontSize()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui.color.search);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui.color.field_hover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui.color.search);
    ImGui::PushStyleColor(ImGuiCol_Border, ui.color.search_border);
    ImGui::PushStyleColor(ImGuiCol_Button, ui.color.search);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.field_hover);
    ImGui::SetCursorScreenPos(g.slot(1));
    ImGui::SetNextItemWidth(field_w);
    if (ImGui::BeginCombo("##transform_orientation", TRANSFORM_ORIENTATION_NAMES[orientation])) {
        for (int i = 0; i < (int)std::size(TRANSFORM_ORIENTATION_NAMES); i++) {
            if (ImGui::Selectable(TRANSFORM_ORIENTATION_NAMES[i], i == orientation)) scene.viewport.gizmo_world = i == 0;
        }
        ImGui::EndCombo();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Transform orientation");
    if (ctx.editor_camera) {
        ImGui::SetCursorScreenPos(g.slot(0));
        ImGui::SetNextItemWidth(field_w);
        ImGui::DragFloat("##camera_speed", &ctx.editor_camera->speed, 0.1f, EditorCamera::MIN_SPEED, EditorCamera::MAX_SPEED, "%.1f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Camera fly speed");
    }
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(2);
    g.column(field_w);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::EndChild();
}

void Workspace::_draw_tabs(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    struct Tab { int32_t index; EditorDocument* document; std::string name; };
    std::vector<Tab> entries;
    entries.push_back({ SceneDocument::SCENE, nullptr, scene._name.empty() ? std::string("No Scene") : scene._name });
    for (int32_t i = 0; i < (int32_t)scene.templates.size(); i++) entries.push_back({ i, nullptr, scene.templates[i]->name() });
    if (ctx.documents) {
        for (auto& d : ctx.documents->documents) {
            const std::filesystem::path* p = d->asset_path();
            entries.push_back({ SceneDocument::SCENE, d.get(), p ? p->filename().string() : std::string(d->title()) });
        }
    }

    std::vector<PanelTab> tabs(entries.size());
    std::string single;
    for (size_t i = 0; i < entries.size(); i++) {
        const Tab& e = entries[i];
        PanelTab& t = tabs[i];
        t.name = e.name.c_str();
        if (e.document) {
            const std::filesystem::path* p = e.document->asset_path();
            t.icon = p ? ContentBrowserPanel::icon_for(*p, false) : ICON_FA_FILE;
            t.active = e.document == active_document;
            t.closable = true;
            t.dirty = e.document->dirty();
        } else if (e.index >= 0) {
            t.icon = ICON_FA_BOX;
            t.active = !active_document && scene.active == e.index;
            t.closable = true;
            t.dirty = scene.templates[e.index]->dirty();
        } else {
            t.icon = ICON_FA_DESKTOP;
            t.active = !active_document && scene.active == SceneDocument::SCENE;
            t.dirty = scene.scene_dirty();
        }
    }
    if (entries.size() == 1) {
        const RenderView& v = ctx.renderer->main_view;
        char title[256];
        std::snprintf(title, sizeof(title), "Engine View 1 (%ux%u) - %s", v.output_width, v.output_height, entries[0].name.c_str());
        single = title;
        tabs[0].name = single.c_str();
    }

    const PanelTabResult r = panel_tab_bar(tabs.data(), (int)tabs.size(), p_min, p_max, true);
    if (r.clicked >= 0) {
        const Tab& e = entries[r.clicked];
        active_document = e.document;
        if (!e.document) scene.activate(ctx, e.index);
    }
    if (r.closed >= 0) {
        const Tab& e = entries[r.closed];
        if (e.document) ctx.documents->close(e.document->kind, e.document->guid);
        else if (e.index >= 0) scene.close_template(ctx, e.index, false);
    }
    if (r.menu) ImGui::OpenPopup("##viewport_menu");
    Viewport::draw_options_menu(ctx);
}

void Workspace::_draw_center(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    const float header_h = panel_header_height();
    if (p_max.y - p_min.y < header_h + 2.0f || p_max.x - p_min.x < 2.0f) return;
    _draw_tabs(ctx, p_min, ImVec2(p_max.x, p_min.y + header_h));
    const ImVec2 body(p_min.x, p_min.y + header_h);

    EditorDocument* d = active_document;
    if (!d) {
        scene.draw_view(ctx, body, p_max);
        return;
    }
    const ImVec2 bar_max(p_max.x, body.y + ui.row);
    if (bar_max.y + 2.0f > p_max.y) return;
    ImGui::GetWindowDrawList()->AddRectFilled(body, bar_max, ui.color.view_bar);
    Viewport::draw_bar(body, bar_max, false);
    const ImVec2 view_min(body.x, bar_max.y);
    ImGui::GetWindowDrawList()->AddRectFilled(view_min, p_max, ui.color.panel);
    ImGui::SetCursorScreenPos(view_min);
    ImGui::PushID(d);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::BeginChild("##document_view", ImVec2(p_max.x - view_min.x, p_max.y - view_min.y), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar();
    if (visible && d->_created) {
        const bool lock = d->locked(ctx);
        if (lock) ImGui::BeginDisabled();
        d->on_update(ctx, ImGui::GetIO().DeltaTime);
        if (lock) ImGui::EndDisabled();
        if (viewport_view().stats && d->preview_scene()) Viewport::draw_stats(ctx, view_min);
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }
    ImGui::EndChild();
    ImGui::PopID();
}

void Workspace::draw(EditorContext& ctx)
{
    _sync_document_panels(ctx);
    layout.center = [this](EditorContext& r_ctx, ImVec2 p_min, ImVec2 p_max) { _draw_center(r_ctx, p_min, p_max); };
    layout.draw(ctx, [this](EditorContext& r_ctx, ImVec2 p_min, ImVec2 p_max) { _draw_toolbar(r_ctx, p_min, p_max); });
    scene.end_frame();
}

}
