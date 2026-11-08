#include <editor/tabs/world/world_tab.h>
#include <editor/ui/tab_strip.h>
#include <editor/ui/tab_toolbar.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/ui_style.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <editor/panels/shared_panels.h>
#include <editor/assets/asset_registry.h>
#include <editor/tabs/world/scene/editor_folders.h>
#include <core/world/world_file.h>
#include <drivers/windows/dialogs_win32.h>
#include <drivers/windows/window_driver_win32.h>
#include <editor/tabs/settings/editor_settings_tab.h>
#include <editor/tabs/tab_manager.h>
#include <editor/tabs/world/panels/details_panel.h>
#include <editor/tabs/world/panels/outliner_panel.h>
#include <editor/tabs/world/panels/world_settings_panel.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <editor/play/editor_play.h>
#include <editor/tabs/world/scene/editor_spawn.h>
#include <core/world/components.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <iterator>

namespace lumen {

Error WorldTab::initialize(SharedPanels& r_shared)
{
    using enum Error;
    layout.initialize("##EditorHost");
    layout.add_panel<OutlinerPanel>();
    details = layout.add_panel<DetailsPanel>();
    layout.add_panel<WorldSettingsPanel>();
    for (auto& p : r_shared.panels) layout.add_shared(p.get());
    layout.center = [this](EditorContext& r_ctx, ImVec2 p_min, ImVec2 p_max) {
        if (!has_world()) {
            _draw_empty(p_min, p_max);
            return;
        }
        layout.viewport.play = r_ctx.play;
        layout.viewport.overlay = [this, &r_ctx](ImDrawList* p_dl, ImVec2 p_pos, ImVec2 p_size) { _draw_play_badge(r_ctx, p_dl, p_pos, ImVec2(p_pos.x + p_size.x, p_pos.y + p_size.y)); };
        layout.viewport.draw(r_ctx, p_min, p_max);
    };
    return OK;
}

void WorldTab::shutdown()
{
    layout.shutdown();
}

void WorldTab::open(Guid p_world)
{
    _pending = p_world;
    _has_pending = true;
}

void WorldTab::show(EditorContext& ctx, Guid p_world)
{
    const auto& list = ctx.tabs->tabs;
    if (std::find(list.begin(), list.end(), this) == list.end()) {
        reset();
        ctx.tabs->add(this, KIND);
    }
    open(p_world);
    ctx.tabs->focus(this);
}

void WorldTab::destroy(EditorContext& ctx)
{
    layout.viewport.cursor_lock.end();
    _unload(ctx);
    reset();
}

void WorldTab::reset()
{
    guid = Guid{};
    _pending = Guid{};
    _has_pending = false;
    _hash = 0;
    _saved_hash = 0;
    _title.clear();
    _tooltip.clear();
}

const std::filesystem::path* WorldTab::_path(EditorContext& ctx) const
{
    const AssetRegistry::Entry* e = ctx.assets ? ctx.assets->find(guid) : nullptr;
    return e && e->type == AssetType::WORLD ? &e->path : nullptr;
}

uint64_t WorldTab::_compute_hash(EditorContext& ctx) const
{
    std::vector<WorldFolder> folders;
    ctx.folders->to_file(folders);
    return WorldFile::hash(*ctx.world, folders);
}

bool WorldTab::_confirm_discard() const
{
    std::string_view name = title();
    if (const size_t gap = name.find("  "); gap != std::string_view::npos) name.remove_prefix(gap + 2);
    const std::wstring heading = L"'" + drivers::WindowDriverWin32::utf8_to_wide(name) + L"' has unsaved changes";
    return drivers::Win32Dialogs::ask(L"Lumen Editor", heading.c_str(), L"Opening another world will discard them. Save first with Ctrl+S to keep them.", L"Discard Changes", L"Cancel");
}

void WorldTab::_unload(EditorContext& ctx)
{
    ctx.world->unload();
    ctx.folders->clear();
    ctx.selection->clear();
    guid = Guid{};
    _hash = _saved_hash = 0;
}

void WorldTab::_load(EditorContext& ctx, Guid p_world)
{
    const AssetRegistry::Entry* e = ctx.assets->find(p_world);
    if (!e || e->type != AssetType::WORLD) {
        log_write("World: no world with guid %s.", p_world.to_string().c_str());
        return;
    }
    _unload(ctx);
    WorldFile file;
    if (WorldFile::read(e->path, *ctx.world, true, file) != Error::OK) {
        log_write("World: failed to load '%s'.", e->path.string().c_str());
        ctx.world->unload();
        return;
    }
    ctx.folders->from_file(file.folders);
    guid = p_world;
    _hash = _saved_hash = _compute_hash(ctx);
    _hash_poll = ImGui::GetTime();
}

Error WorldTab::write(EditorContext& ctx, const std::filesystem::path& p_path) const
{
    WorldFile file;
    file.guid = guid;
    ctx.folders->to_file(file.folders);
    return WorldFile::write(p_path, *ctx.world, file);
}

void WorldTab::_draw_empty(ImVec2 p_min, ImVec2 p_max)
{
    ImGui::GetWindowDrawList()->AddRectFilled(p_min, p_max, ui_style().color.sunken);
}

void WorldTab::_draw_play_badge(EditorContext& ctx, ImDrawList* p_dl, ImVec2 p_min, ImVec2 p_max)
{
    if (!ctx.play || !ctx.play->active()) return;
    const UiStyle& ui = ui_style();
    const bool playing = ctx.play->playing();
    const ImU32 color = playing ? ui.color.success : ui.color.warning;
    const char* text = playing ? ICON_FA_PLAY "  PLAYING    F8 edit    Shift+F5 stop" : ICON_FA_PAUSE "  EDITING  game paused    F8 / F5 resume    Shift+F5 stop";

    ImDrawList* dl = p_dl;
    dl->AddRect(p_min, p_max, color, 0.0f, 0, 2.0f);
    const ImVec2 ts = ImGui::CalcTextSize(text);
    const float pad = ui.overlay.margin;
    const ImVec2 a(ImFloor((p_min.x + p_max.x - ts.x) * 0.5f - pad * 2.0f), p_min.y + pad * 2.0f);
    const ImVec2 b(a.x + ts.x + pad * 4.0f, a.y + ts.y + pad * 2.0f);
    dl->AddRectFilled(a, b, ui_with_alpha(ui.color.sunken, 0.85f), ui.play.rounding);
    dl->AddRect(a, b, color, ui.play.rounding);
    dl->AddText(ImVec2(a.x + pad * 2.0f, a.y + pad), color, text);
}

void WorldTab::on_tick(EditorContext& ctx)
{
    if (!ctx.assets || ctx.assets->dirty) return;
    if (_has_pending) {
        _has_pending = false;
        if (_pending != guid && (!dirty() || _confirm_discard())) _load(ctx, _pending);
    }
    if (has_world() && !_path(ctx)) _unload(ctx);
    if (!has_world() && !_has_pending) {
        ctx.tabs->close(KIND, guid, true);
        return;
    }

    const double now = ImGui::GetTime();
    if (has_world() && now - _hash_poll >= DIRTY_POLL_S) {
        _hash_poll = now;
        _hash = _compute_hash(ctx);
    }

    const AssetRegistry::Entry* e = ctx.assets->find(guid);
    if (!e) {
        _title.clear();
        _tooltip.clear();
        return;
    }
    _title = ICON;
    _title += "  ";
    _title += e->name;
    char guid_text[Guid::BUFFER];
    guid.to_chars(guid_text);
    _tooltip = e->asset_path + "\n" + guid_text;
}

struct PlayButton
{
    const char* label;
    const char* tooltip;
    bool enabled;
    ImU32 icon;
};

static bool _play_button(const PlayButton& p_button, ImVec2 p_min, ImVec2 p_size, bool p_open, ImDrawFlags p_corners)
{
    ImGui::SetCursorScreenPos(p_min);
    const bool clicked = ImGui::InvisibleButton(p_button.label, p_size);
    const bool hovered = p_button.enabled && ImGui::IsItemHovered();
    const bool held = p_button.enabled && ImGui::IsItemActive();
    if (p_button.tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", p_button.tooltip);

    const UiStyle& ui = ui_style();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(p_min.x + p_size.x, p_min.y + p_size.y);
    const ImU32 fill = (held || p_open) ? ui.color.accent : (hovered ? ui.color.menu_field_hover : ui.color.menu_field);
    dl->AddRectFilled(p_min, max, fill, ui.header.rounding, p_corners);

    const ImU32 col = p_button.enabled ? p_button.icon : ui_with_alpha(ui.color.text_dim, ui.alpha.disabled);
    ui_text_centered(dl, p_min, max, col, p_button.label, ImGui::FindRenderedTextEnd(p_button.label));
    return clicked && p_button.enabled;
}

void WorldTab::_draw_play_controls(EditorContext& ctx, float p_center_x, float p_y)
{
    const UiStyle& ui = ui_style();
    const bool playing = ctx.pie_is_playing && ctx.pie_is_playing();
    const bool paused = playing && ctx.pie_is_paused && ctx.pie_is_paused();

    const float h = ImGui::GetFrameHeight();
    const float bw = ImFloor(h * 1.3f);
    const float total_w = bw * 3.0f;
    const ImVec2 box_min(ImFloor(p_center_x - total_w * 0.5f), p_y);
    const ImVec2 box_max(box_min.x + total_w, box_min.y + h);

    ImGui::PushID("##play_controls");
    const ImVec2 size(bw, h);
    float x = box_min.x;

    PlayButton primary{ ICON_FA_PLAY "##primary", has_world() ? "Play (F5)" : "No world open", has_world() && (bool)ctx.pie_toggle_play, ui.color.success_text };
    if (playing && paused) primary = { ICON_FA_PLAY "##primary", "Resume playing (F8)", (bool)ctx.pie_toggle_pause, ui.color.success_text };
    else if (playing) primary = { ICON_FA_PAUSE "##primary", "Pause and edit (F8)", (bool)ctx.pie_toggle_pause, ui.color.text };
    if (_play_button(primary, ImVec2(x, p_y), size, false, ImDrawFlags_RoundCornersLeft)) {
        if (!playing) ctx.pie_toggle_play();
        else ctx.pie_toggle_pause();
    }
    x += bw;

    const PlayButton stop{ ICON_FA_STOP "##stop", "Stop (Shift+F5)", playing && (bool)ctx.pie_toggle_play, ui.color.text };
    if (_play_button(stop, ImVec2(x, p_y), size, false, ImDrawFlags_RoundCornersNone)) ctx.pie_toggle_play();
    x += bw;

    const bool options_open = ImGui::IsPopupOpen("##options_menu");
    const PlayButton options{ ICON_FA_ANGLE_DOWN "##options", "Play options", true, ui.color.text };
    if (_play_button(options, ImVec2(x, p_y), size, options_open, ImDrawFlags_RoundCornersRight)) ImGui::OpenPopup("##options_menu");

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRect(box_min, box_max, ui.color.outline, ui.header.rounding);
    dl->AddLine(ImVec2(box_min.x + bw, box_min.y), ImVec2(box_min.x + bw, box_max.y), ui.color.outline);
    dl->AddLine(ImVec2(box_min.x + bw * 2.0f, box_min.y), ImVec2(box_min.x + bw * 2.0f, box_max.y), ui.color.outline);

    ImGui::SetNextWindowPos(ImVec2(x, box_max.y + ui.menu.popup_offset), ImGuiCond_Always);
    ui_menu_push_style();
    if (ImGui::BeginPopup("##options_menu", ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::TextDisabled("Modes");
        ImGui::MenuItem(ICON_FA_DISPLAY "  In Editor", nullptr, true);
        ImGui::EndPopup();
    }
    ui_menu_pop_style();

    ImGui::PopID();
}

void WorldTab::_draw_header_menus(EditorContext& ctx, TabToolbar& r_bar)
{
    const bool world = has_world();

    if (r_bar.menu("Select", world)) {
        if (ImGui::MenuItem("All") && ctx.world && ctx.selection) {
            ctx.selection->clear();
            ctx.world->view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) {
                if (!ctx.world->has<EditorHiddenTag>(p_entity)) ctx.selection->add(p_entity);
            });
        }
        if (ImGui::MenuItem("None") && ctx.selection) ctx.selection->clear();
        if (ImGui::MenuItem("Environment") && ctx.selection) ctx.selection->select_environment();
        r_bar.end_menu();
    }
    if (r_bar.menu("Add", world)) {
        const Entity e = editor_spawn_menu_items(ctx, EditorFolders::ROOT);
        if (e != ENTITY_NULL && ctx.selection) ctx.selection->select(e);
        r_bar.end_menu();
    }
}

void WorldTab::_draw_toolbar(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    TabToolbar bar;
    bar.begin();

    Viewport& vp = layout.viewport;
    const int orientation = vp.gizmo_world ? 0 : 1;
    float orientation_w = 0.0f;
    for (const char* n : TRANSFORM_ORIENTATION_NAMES) orientation_w = ImMax(orientation_w, ImGui::CalcTextSize(n).x);
    orientation_w += ImGui::GetStyle().FramePadding.x * 3.0f + ui.header.dropdown_arrow * 2.0f;
    if (bar.dropdown("##transform_orientation", TRANSFORM_ORIENTATION_NAMES[orientation], orientation_w)) {
        for (int i = 0; i < (int)std::size(TRANSFORM_ORIENTATION_NAMES); i++) {
            if (ImGui::MenuItem(TRANSFORM_ORIENTATION_NAMES[i], nullptr, i == orientation)) vp.gizmo_world = i == 0;
        }
        bar.end_dropdown();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Transform orientation");

    const int mode = (int)edit_mode;
    float mode_w = 0.0f;
    for (const char* n : WORLD_EDIT_MODE_NAMES) mode_w = ImMax(mode_w, ImGui::CalcTextSize(n).x);
    mode_w += ImGui::GetStyle().FramePadding.x * 3.0f + ui.header.dropdown_arrow * 2.0f;
    if (bar.dropdown("##edit_mode", WORLD_EDIT_MODE_NAMES[mode], mode_w)) {
        for (int i = 0; i < (int)WorldEditMode::COUNT; i++) {
            if (ImGui::MenuItem(WORLD_EDIT_MODE_NAMES[i], nullptr, i == mode)) edit_mode = (WorldEditMode)i;
        }
        bar.end_dropdown();
    }
    bar.gap();
    _draw_header_menus(ctx, bar);

    if (has_world()) {
        const float controls_w = ImFloor(ImGui::GetFrameHeight() * 1.3f) * 3.0f;
        const float center_x = ImMax(ImFloor((bar.min.x + bar.max.x) * 0.5f), bar.x + ui.header.group_gap + controls_w * 0.5f);
        _draw_play_controls(ctx, center_x, bar._row_y());
        layout.viewport.draw_header_controls(ctx, ImVec2(center_x + controls_w * 0.5f, bar.min.y), bar.max);
    }
    bar.end();
}

Error WorldTab::save(EditorContext& ctx)
{
    using enum Error;
    if (!has_world()) return OK;
    const std::filesystem::path* path = _path(ctx);
    if (!path) return FAILED;
    if (write(ctx, *path) != OK) {
        log_write("World: failed to save '%s'.", path->string().c_str());
        return FAILED;
    }
    _hash = _saved_hash = _compute_hash(ctx);
    return OK;
}

void WorldTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    if (ctx.selection && ctx.selection->version != selection_version) {
        selection_version = ctx.selection->version;
        if (!ctx.selection->empty() || ctx.selection->environment) layout.well(details->default_zone()).active_name = details->layout_id;
    }
    const bool select = edit_mode == WorldEditMode::SELECTION;
    layout.viewport.allow_pick = select;
    layout.viewport.allow_gizmo = select;
    layout.draw(ctx, [this](EditorContext& r_ctx) { _draw_toolbar(r_ctx); });
}

void WorldTab::draw_menu()
{
    layout.draw_menu();
}

}
