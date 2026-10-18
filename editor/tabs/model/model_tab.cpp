#include <editor/tabs/model/model_tab.h>
#include <editor/tabs/model/panels/model_details_panel.h>
#include <editor/drawer/drawer.h>
#include <editor/drawer/content_browser_tab.h>
#include <editor/tabs/tab_manager.h>
#include <editor/assets/asset_registry.h>
#include <core/base/error.h>
#include <imgui.h>
#include <cstdio>

namespace lumen {

Error ModelTab::create(EditorContext& ctx)
{
    using enum Error;
    if (path.empty()) {
        const AssetRegistry::Entry* e = ctx.assets->find(guid);
        if (!e) return FAILED;
        path = e->path;
    }
    if (model.load(path) != OK || !(model.guid == guid)) {
        log_write("Model Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = model;
    _refresh_title();

    char host_id[48];
    std::snprintf(host_id, sizeof(host_id), "##ModelHost_%016llx", (unsigned long long)guid.value);
    layout.initialize(host_id);
    layout.viewport.allow_pick = false;
    layout.viewport.allow_outline = false;
    layout.viewport.allow_gizmo = false;
    layout.viewport.allow_look = true;
    layout.keep_right_dock = true;
    layout.drawer.allowed = { ctx.drawer->content_browser };
    layout.add_panel<ModelDetailsPanel>()->tab = this;
    orbit.update();
    return OK;
}

void ModelTab::destroy(EditorContext& ctx)
{
    (void)ctx;
    layout.viewport.cursor_lock.end();
    layout.shutdown();
}

Error ModelTab::save(EditorContext& ctx)
{
    (void)ctx;
    if (model.save(path) != Error::OK) {
        log_write("Model Tab: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = model;
    _refresh_title();
    return Error::OK;
}

void ModelTab::on_tick(EditorContext& ctx)
{
    if (!ctx.assets->dirty) {
        const AssetRegistry::Entry* e = ctx.assets->find(guid);
        if (!e) {
            ctx.tabs->close(KIND, guid, true);
            return;
        }
        path = e->path;
    }
    _refresh_title();
}

void ModelTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    layout.draw(ctx, nullptr);
    if (layout.viewport.looking) orbit.rotate(layout.viewport.look_delta.x, layout.viewport.look_delta.y);
    if (layout.viewport.look_zoom != 0.0f) orbit.zoom(layout.viewport.look_zoom);
    orbit.update();
}

void ModelTab::_refresh_title()
{
    _title = ICON;
    _title += "  ";
    _title += path.stem().string();
    if (dirty()) _title += '*';
}

}