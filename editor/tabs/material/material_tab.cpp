#include <editor/tabs/material/material_tab.h>
#include <core/base/error.h>
#include <core/rendering/renderer.h>
#include <editor/assets/builtin_meshes.h>
#include <editor/tabs/material/panels/material_details_panel.h>

namespace lumen {

Error MaterialTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    if (material.load(path) != OK || material.guid != guid) {
        log_write("Material Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = material;
    _refresh_title();

    _create_layout(ctx, "MaterialHost");
    layout.viewport.allow_grid = false;
    layout.add_panel<MaterialDetailsPanel>()->tab = this;

    _create_preview();
    if (ctx.renderer && BuiltinMeshes::ensure_sphere()) {
        ctx.renderer->geometry.load(BuiltinMeshes::SPHERE, BuiltinMeshes::sphere_bin());
        ctx.renderer->models.set(_preview_model, BuiltinMeshes::SPHERE, { material.guid });
    }

    orbit.update();
    return OK;
}

void MaterialTab::destroy(EditorContext& ctx)
{
    if (ctx.renderer && dirty()) ctx.renderer->materials.set(saved);
    AssetTab::destroy(ctx);
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

void MaterialTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    layout.draw(ctx, nullptr);
    _update_orbit();
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

}
