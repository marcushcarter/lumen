#include <editor/tabs/model/model_tab.h>
#include <core/base/error.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <editor/tabs/model/panels/model_details_panel.h>
#include <imgui.h>

namespace lumen {

Error ModelTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    if (model.load(path) != OK || model.guid != guid) {
        log_write("Model Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = model;
    _refresh_title();

    _create_layout(ctx, "ModelHost");
    layout.add_panel<ModelDetailsPanel>()->tab = this;

    _create_preview();
    _preview_mesh = model.mesh;
    _preview_materials = model.materials;
    ctx.renderer->models.set(_preview_model, _preview_mesh, _preview_materials);

    orbit.update();
    return OK;
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

void ModelTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    layout.draw(ctx, nullptr);
    _update_orbit();
    _sync_preview(ctx);
    _refresh_stats(ctx);
    orbit.update();
}

void ModelTab::_refresh_stats(EditorContext& ctx)
{
    const double now = ImGui::GetTime();
    const bool changed = model.mesh != _stats_mesh;
    if (!changed && now - _stats_poll < STATS_POLL_S) return;
    _stats_poll = now;

    if (!model.mesh.valid() || !ctx.project) {
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
    if (model.mesh != _preview_mesh || model.materials != _preview_materials) {
        _preview_mesh = model.mesh;
        _preview_materials = model.materials;
        ctx.renderer->models.set(_preview_model, _preview_mesh, _preview_materials);
    }
    if (_framed_mesh == _preview_mesh) return;
    if (const LMesh* mesh = ctx.renderer->geometry.get(ctx.renderer->geometry.find(_preview_mesh))) {
        orbit.frame(glm::vec3(mesh->bounds_sphere), mesh->bounds_sphere.w);
        _framed_mesh = _preview_mesh;
    }
}

}
