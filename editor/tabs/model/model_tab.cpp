#include <editor/tabs/model/model_tab.h>
#include <editor/tabs/model/panels/model_details_panel.h>
#include <editor/drawer/drawer.h>
#include <editor/drawer/content_browser_tab.h>
#include <editor/tabs/tab_manager.h>
#include <editor/assets/asset_registry.h>
#include <core/base/error.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <core/project/project.h>
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

    preview.initialize();
    preview.load();
    preview.atmosphere.time_of_day = 12.0f;
    _preview_model = Guid::generate();
    _preview_mesh = model.mesh;
    ctx.renderer->models.set(_preview_model, _preview_mesh);
    const Entity e = preview.create();
    preview.add<TransformComponent>(e);
    preview.add<ModelComponent>(e, { _preview_model });
    preview.add<StaticTag>(e);

    orbit.update();
    return OK;
}

void ModelTab::destroy(EditorContext& ctx)
{
    layout.viewport.cursor_lock.end();
    layout.shutdown();
    if (ctx.renderer) ctx.renderer->models.unload(_preview_model);
    preview.shutdown();
}

Error ModelTab::save(EditorContext& ctx)
{
    if (model.save(path) != Error::OK) {
        log_write("Model Tab: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    if (ctx.renderer) ctx.renderer->models.load(path);
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
    _sync_preview(ctx);
    _refresh_stats(ctx);
    orbit.update();
}

void ModelTab::_refresh_stats(EditorContext& ctx)
{
    const double now = ImGui::GetTime();
    const bool changed = !(model.mesh == _stats_mesh);
    if (!changed && now - _stats_poll < STATS_POLL_S) return;
    _stats_poll = now;

    if (model.mesh == Guid{} || !ctx.project) {
        mesh_stats = {};
        _stats_mesh = model.mesh;
        _stats_time = {};
        return;
    }

    const std::filesystem::path bin = ctx.project->content_path(model.mesh);
    std::error_code ec;
    const std::filesystem::file_time_type t = std::filesystem::last_write_time(bin, ec);
    if (!changed && !ec && t == _stats_time) return;

    _stats_mesh = model.mesh;
    _stats_time = ec ? std::filesystem::file_time_type{} : t;
    if (!lmesh_read_stats(bin, mesh_stats)) mesh_stats = {};
}

void ModelTab::_sync_preview(EditorContext& ctx)
{
    if (!ctx.renderer) return;
    if (!(model.mesh == _preview_mesh)) {
        _preview_mesh = model.mesh;
        ctx.renderer->models.set(_preview_model, _preview_mesh);
    }
    if (_framed_mesh == _preview_mesh) return;
    if (const LMesh* mesh = ctx.renderer->geometry.get(ctx.renderer->geometry.find(_preview_mesh))) {
        orbit.frame(glm::vec3(mesh->bounds_sphere), mesh->bounds_sphere.w);
        _framed_mesh = _preview_mesh;
    }
}

void ModelTab::_refresh_title()
{
    _title = ICON;
    _title += "  ";
    _title += path.stem().string();
}

}