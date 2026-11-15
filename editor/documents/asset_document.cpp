#include <editor/documents/asset_document.h>
#include <core/rendering/renderer.h>
#include <core/project/project.h>
#include <core/scene/components.h>
#include <editor/assets/asset_registry.h>
#include <editor/documents/document_manager.h>
#include <editor/assets/import/reimport.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <toml++/toml.hpp>
#include <cstdio>

namespace lumen {

bool AssetDocument::_resolve_path(EditorContext& ctx)
{
    if (!path.empty()) return true;
    const AssetRegistry::Entry* e = ctx.assets->find(guid);
    if (!e) return false;
    path = e->path;
    return true;
}

void AssetDocument::_create_layout()
{
    layout.center = [this](EditorContext& r_ctx, ImVec2 p_min, ImVec2 p_max) { viewport.draw(r_ctx, p_min, p_max); };
}

void AssetDocument::_create_preview()
{
    preview.initialize();
    preview.load();
    _preview_model = Guid::generate();
    _preview_entity = preview.create();
    preview.add<TransformComponent>(_preview_entity);
    preview.add<ModelComponent>(_preview_entity, { _preview_model });
    preview.add<StaticTag>(_preview_entity);
}

void AssetDocument::_update_orbit()
{
    if (viewport.looking) orbit.rotate(viewport.look_delta.x, viewport.look_delta.y);
    if (viewport.look_zoom != 0.0f) orbit.zoom(viewport.look_zoom);
}

void AssetDocument::_refresh_title()
{
    _title = path.stem().string();
    _title += " - ";
    _title += _type_name();
}

void AssetDocument::restore_view(const OrbitCamera& p_orbit)
{
    orbit = p_orbit;
    orbit.keep_view = true;
}

bool AssetDocument::locked(const EditorContext& ctx) const
{
    return ctx.imports && !path.empty() && ctx.imports->importing(path);
}

void AssetDocument::_draw_lock(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p_min, p_max, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));

    const float fs = ImGui::GetFontSize();
    const ImVec2 size(ImFloor(fs * 18.0f), ImFloor(fs * 6.0f));
    const ImVec2 center(ImFloor((p_min.x + p_max.x) * 0.5f), ImFloor((p_min.y + p_max.y) * 0.5f));
    const ImVec2 a(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
    const ImVec2 b(a.x + size.x, a.y + size.y);
    const float pad = ImFloor(fs);
    dl->AddRectFilled(a, b, ui.color.panel);
    dl->AddRect(a, b, ui.color.outline);

    const float progress = ImMax(ctx.imports->progress(path), 0.0f);
    char label[64];
    std::snprintf(label, sizeof(label), "Reimporting... %d%%", (int)(progress * 100.0f));
    dl->AddText(ImVec2(a.x + pad, a.y + pad), ui.color.text, label);

    const float bar_y = a.y + pad + ImGui::GetTextLineHeight() + ImFloor(pad * 0.5f);
    const ImVec2 bar0(a.x + pad, bar_y);
    const ImVec2 bar1(b.x - pad, bar_y + ImFloor(fs * 0.35f));
    dl->AddRectFilled(bar0, bar1, ui.color.field);
    if (progress > 0.0f) dl->AddRectFilled(bar0, ImVec2(bar0.x + (bar1.x - bar0.x) * progress, bar1.y), ui.color.accent);

    const ImVec2 btn_size(ImFloor(fs * 6.0f), ImGui::GetFrameHeight());
    const ImVec2 btn0(b.x - pad - btn_size.x, b.y - pad - btn_size.y);
    const ImVec2 btn1(btn0.x + btn_size.x, btn0.y + btn_size.y);
    const bool hovered = ImGui::IsMouseHoveringRect(btn0, btn1, false);
    dl->AddRectFilled(btn0, btn1, hovered ? ui.color.field_hover : ui.color.field);
    dl->AddRect(btn0, btn1, ui.color.outline);
    ui_text_centered(dl, btn0, btn1, ui.color.text, "Cancel", nullptr);
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) ctx.imports->cancel(path);
}

void AssetDocument::_draw_layout(EditorContext& ctx)
{
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 end(origin.x + ImMax(1.0f, avail.x), origin.y + ImMax(1.0f, avail.y));
    if (layout.center) layout.center(ctx, origin, end);
    if (locked(ctx)) {
        ImGui::EndDisabled();
        _draw_lock(ctx, origin, end);
        ImGui::BeginDisabled();
    }
}

void AssetDocument::destroy(EditorContext& ctx)
{
    viewport.release();
    layout.shutdown();
    if (ctx.renderer && _preview_model.valid()) ctx.renderer->models.unload(_preview_model);
    preview.shutdown();
}

void AssetDocument::on_tick(EditorContext& ctx)
{
    if (!ctx.assets->dirty) {
        const AssetRegistry::Entry* e = ctx.assets->find(guid);
        if (!e) {
            ctx.documents->close(kind, guid, true);
            return;
        }
        path = e->path;
        _tooltip = e->asset_path;
    }
    _refresh_title();
    _refresh_info(ctx);
}

void AssetDocument::draw_import() const
{
    ImGui::TextUnformatted("Source:");
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", source.empty() ? "Unknown" : source.c_str());
    ImGui::PopTextWrapPos();
}

void AssetDocument::_refresh_info(EditorContext& ctx)
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
