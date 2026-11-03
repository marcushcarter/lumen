#include <editor/tabs/asset_tab.h>
#include <core/rendering/renderer.h>
#include <core/project/project.h>
#include <core/world/components.h>
#include <editor/assets/asset_registry.h>
#include <editor/panels/content_browser_panel.h>
#include <editor/panels/shared_panels.h>
#include <editor/tabs/tab_manager.h>
#include <editor/ui/tab_toolbar.h>
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

void AssetTab::_draw_layout(EditorContext& ctx)
{
    layout.draw(ctx, [this](EditorContext& r_ctx) { _draw_toolbar(r_ctx); });
}

void AssetTab::_draw_toolbar(EditorContext& ctx)
{
    TabToolbar bar;
    bar.begin();
    bar.save(ctx, *this);
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
    }
    _refresh_title();
    _refresh_info(ctx);
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
}

}
