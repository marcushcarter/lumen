#include <editor/tabs/asset_tab.h>
#include <core/rendering/renderer.h>
#include <core/project/project.h>
#include <core/world/components.h>
#include <editor/assets/asset_registry.h>
#include <editor/panels/content_browser_panel.h>
#include <editor/panels/shared_panels.h>
#include <editor/tabs/tab_manager.h>
#include <editor/ui/tab_toolbar.h>
#include <editor/assets/import/reimport.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <toml++/toml.hpp>
#include <cstdio>

namespace lumen {

bool AssetTab::_resolve_path(EditorContext& ctx)
{
    if (!path.empty()) return true;
    const AssetRegistry::Entry* e = ctx.assets->find(guid);
    if (!e) return false;
    path = e->path;
    return true;
}

void AssetTab::_create_layout(EditorContext& ctx, const char* p_host_prefix)
{
    char host_id[48];
    std::snprintf(host_id, sizeof(host_id), "##%s_%016llx", p_host_prefix, (unsigned long long)guid.value);
    layout.initialize(host_id);
    layout.viewport.allow_pick = false;
    layout.viewport.allow_outline = false;
    layout.viewport.allow_gizmo = false;
    layout.viewport.allow_look = true;
    layout.viewport.allow_asset_drop = false;
    for (auto& p : ctx.shared_panels->panels) layout.add_shared(p.get());
}

void AssetTab::_create_preview()
{
    preview.initialize();
    preview.load();
    _preview_model = Guid::generate();
    _preview_entity = preview.create();
    preview.add<TransformComponent>(_preview_entity);
    preview.add<ModelComponent>(_preview_entity, { _preview_model });
    preview.add<StaticTag>(_preview_entity);
}

void AssetTab::_update_orbit()
{
    if (layout.viewport.looking) orbit.rotate(layout.viewport.look_delta.x, layout.viewport.look_delta.y);
    if (layout.viewport.look_zoom != 0.0f) orbit.zoom(layout.viewport.look_zoom);
}

void AssetTab::_refresh_title()
{
    _title = _icon();
    _title += "  ";
    _title += path.stem().string();
}

void AssetTab::restore_view(const OrbitCamera& p_orbit)
{
    orbit = p_orbit;
    orbit.keep_view = true;
}

bool AssetTab::locked(const EditorContext& ctx) const
{
    return ctx.imports && !path.empty() && ctx.imports->importing(path);
}

void AssetTab::_draw_lock(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    ImDrawList* dl = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
    dl->AddRectFilled(p_min, p_max, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));

    const float fs = ImGui::GetFontSize();
    const ImVec2 size(ImFloor(fs * 18.0f), ImFloor(fs * 6.0f));
    const ImVec2 center(ImFloor((p_min.x + p_max.x) * 0.5f), ImFloor((p_min.y + p_max.y) * 0.5f));
    const ImVec2 a(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
    const ImVec2 b(a.x + size.x, a.y + size.y);
    const float pad = ImFloor(fs);
    dl->AddRectFilled(a, b, ui.color.panel, ui.panel_box.rounding);
    dl->AddRect(a, b, ui.color.outline, ui.panel_box.rounding);

    const float progress = ImMax(ctx.imports->progress(path), 0.0f);
    char label[64];
    std::snprintf(label, sizeof(label), "Reimporting... %d%%", (int)(progress * 100.0f));
    dl->AddText(ImVec2(a.x + pad, a.y + pad), ui.color.text, label);

    const float bar_y = a.y + pad + ImGui::GetTextLineHeight() + ImFloor(pad * 0.5f);
    const ImVec2 bar0(a.x + pad, bar_y);
    const ImVec2 bar1(b.x - pad, bar_y + ImFloor(fs * 0.35f));
    dl->AddRectFilled(bar0, bar1, ui.color.menu_field, ui.header.rounding);
    if (progress > 0.0f) dl->AddRectFilled(bar0, ImVec2(bar0.x + (bar1.x - bar0.x) * progress, bar1.y), ui.color.accent, ui.header.rounding);

    const ImVec2 btn_size(ImFloor(fs * 6.0f), ImGui::GetFrameHeight());
    const ImVec2 btn0(b.x - pad - btn_size.x, b.y - pad - btn_size.y);
    const ImVec2 btn1(btn0.x + btn_size.x, btn0.y + btn_size.y);
    const bool hovered = ImGui::IsMouseHoveringRect(btn0, btn1, false);
    dl->AddRectFilled(btn0, btn1, hovered ? ui.color.menu_field_hover : ui.color.menu_field, ui.header.rounding);
    dl->AddRect(btn0, btn1, ui.color.outline, ui.header.rounding);
    ui_text_centered(dl, btn0, btn1, ui.color.text, "Cancel", nullptr);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ctx.imports->cancel(path);
}

void AssetTab::_draw_layout(EditorContext& ctx)
{
    const bool is_locked = locked(ctx);
    if (is_locked) ImGui::BeginDisabled();
    layout.draw(ctx, [this](EditorContext& r_ctx) { _draw_toolbar(r_ctx); });
    if (!is_locked) return;
    ImGui::EndDisabled();
    _draw_lock(ctx, layout.area_min, layout.area_max);
}

void AssetTab::_draw_reimport_buttons(EditorContext& ctx, TabToolbar& r_bar)
{
    if (reimport_kind == ReimportKind::NONE) return;
    const bool clean = !dirty();
    if (r_bar.button(ICON_FA_ROTATE "  Reimport", clean ? "Reimport from the source file with the saved settings" : "Save before reimporting", false, clean)) reimport_asset(ctx, path);
    if (reimport_has_settings(reimport_kind) && r_bar.button(ICON_FA_SLIDERS "  Reimport With Settings", clean ? "Change the import settings, then reimport" : "Save before reimporting", false, clean)) reimport_with_settings(ctx, { path });
}

void AssetTab::_draw_toolbar(EditorContext& ctx)
{
    TabToolbar bar;
    bar.begin();
    bar.save(ctx, *this);
    _draw_reimport_buttons(ctx, bar);
    bar.end();
}

void AssetTab::destroy(EditorContext& ctx)
{
    layout.viewport.cursor_lock.end();
    layout.shutdown();
    if (ctx.renderer) ctx.renderer->models.unload(_preview_model);
    preview.shutdown();
}

void AssetTab::on_tick(EditorContext& ctx)
{
    if (!ctx.assets->dirty) {
        const AssetRegistry::Entry* e = ctx.assets->find(guid);
        if (!e) {
            ctx.tabs->close(kind, guid, true);
            return;
        }
        path = e->path;
        _tooltip = e->asset_path;
    }
    _refresh_title();
    _refresh_info(ctx);
}

void AssetTab::draw_import() const
{
    ImGui::TextUnformatted("Source:");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", source.empty() ? "Unknown" : source.c_str());
    ImGui::PopTextWrapPos();
}

void AssetTab::_refresh_info(EditorContext& ctx)
{
    const double now = ImGui::GetTime();
    if (now - _info_poll < INFO_POLL_S) return;
    _info_poll = now;

    std::error_code ec;
    std::filesystem::path shipped = ctx.project ? ctx.project->content_path(guid) : std::filesystem::path();
    if (shipped.empty() || !std::filesystem::exists(shipped, ec)) shipped = path;
    const std::filesystem::file_time_type t = std::filesystem::last_write_time(shipped, ec);
    if (ec || t == _info_time) return;
    _info_time = t;

    const uintmax_t size = std::filesystem::file_size(shipped, ec);
    disk_size = ec ? 0 : (uint64_t)size;

    toml::table tbl;
    try { tbl = toml::parse_file(path.string()); }
    catch (const toml::parse_error&) { return; }
    source = tbl["import"]["source"].value_or(std::string());
    reimport_kind = reimport_target(path).kind;
}

}
