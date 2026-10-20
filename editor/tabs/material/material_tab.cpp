#include <editor/tabs/material/material_tab.h>
#include <editor/tabs/material/panels/material_details_panel.h>
#include <editor/drawer/drawer.h>
#include <editor/drawer/content_browser_tab.h>
#include <editor/tabs/tab_manager.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/builtin_meshes.h>
#include <core/base/error.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <cstdio>

namespace lumen {

Error MaterialTab::create(EditorContext& ctx)
{
    using enum Error;
    if (path.empty()) {
        const AssetRegistry::Entry* e = ctx.assets->find(guid);
        if (!e) return FAILED;
        path = e->path;
    }
    if (material.load(path) != OK || !(material.guid == guid)) {
        log_write("Material Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = material;
    _refresh_title();

    char host_id[48];
    std::snprintf(host_id, sizeof(host_id), "##MaterialHost_%016llx", (unsigned long long)guid.value);
    layout.initialize(host_id);
    layout.viewport.allow_pick = false;
    layout.viewport.allow_outline = false;
    layout.viewport.allow_gizmo = false;
    layout.viewport.allow_look = true;
    layout.keep_right_dock = true;
    layout.drawer.allowed = { ctx.drawer->content_browser };
    layout.add_panel<MaterialDetailsPanel>()->tab = this;

    preview.initialize();
    preview.load();
    preview.atmosphere.time_of_day = 12.0f;
    layout.viewport.time_of_day = &preview.atmosphere.time_of_day;
    _preview_model = Guid::generate();
    if (ctx.renderer && BuiltinMeshes::ensure_sphere()) {
        ctx.renderer->geometry.load(BuiltinMeshes::SPHERE, BuiltinMeshes::sphere_bin());
        ctx.renderer->models.set(_preview_model, BuiltinMeshes::SPHERE, { material.guid });
    }
    const Entity e = preview.create();
    preview.add<TransformComponent>(e);
    preview.add<ModelComponent>(e, { _preview_model });
    preview.add<StaticTag>(e);

    orbit.update();
    return OK;
}

void MaterialTab::destroy(EditorContext& ctx)
{
    layout.viewport.cursor_lock.end();
    layout.shutdown();
    if (ctx.renderer) {
        ctx.renderer->models.unload(_preview_model);
        if (dirty()) ctx.renderer->materials.set(saved);
    }
    preview.shutdown();
}

Error MaterialTab::save(EditorContext& ctx)
{
    (void)ctx;
    if (material.save(path) != Error::OK) {
        log_write("Material Tab: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = material;
    return Error::OK;
}

void MaterialTab::on_tick(EditorContext& ctx)
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

void MaterialTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    layout.draw(ctx, nullptr);
    if (layout.viewport.looking) orbit.rotate(layout.viewport.look_delta.x, layout.viewport.look_delta.y);
    if (layout.viewport.look_zoom != 0.0f) orbit.zoom(layout.viewport.look_zoom);
    _frame_preview(ctx);
    orbit.update();
}

void MaterialTab::_frame_preview(EditorContext& ctx)
{
    if (_framed || !ctx.renderer) return;
    if (const LMesh* mesh = ctx.renderer->geometry.get(ctx.renderer->geometry.find(BuiltinMeshes::SPHERE))) {
        orbit.frame(glm::vec3(mesh->bounds_sphere), mesh->bounds_sphere.w);
        _framed = true;
    }
}

void MaterialTab::_refresh_title()
{
    _title = ICON;
    _title += "  ";
    _title += path.stem().string();
}

}