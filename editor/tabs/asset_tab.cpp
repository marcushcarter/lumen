#include <editor/tabs/asset_tab.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <editor/assets/asset_registry.h>
#include <editor/panels/content_browser_panel.h>
#include <editor/panels/shared_panels.h>
#include <editor/tabs/tab_manager.h>
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
    layout.add_shared(ctx.shared_panels->content_browser);
}

void AssetTab::_create_preview()
{
    preview.initialize();
    preview.load();
    preview.atmosphere.time_of_day = PREVIEW_TIME_OF_DAY;
    layout.viewport.time_of_day = &preview.atmosphere.time_of_day;
    _preview_model = Guid::generate();
    const Entity e = preview.create();
    preview.add<TransformComponent>(e);
    preview.add<ModelComponent>(e, { _preview_model });
    preview.add<StaticTag>(e);
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
}

}
