#include <editor/tabs/mesh/mesh_tab.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <editor/tabs/mesh/panels/mesh_details_panel.h>
#include <imgui.h>

namespace lumen {

Error MeshTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    _refresh_title();

    _create_layout(ctx, "MeshHost");
    layout.add_panel<MeshDetailsPanel>()->tab = this;

    _create_preview();
    ctx.renderer->models.set(_preview_model, guid, {});

    orbit.update();
    return OK;
}

void MeshTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    _draw_layout(ctx);
    _update_orbit();
    _frame(ctx);
    _refresh_stats(ctx);
    orbit.update();
}

void MeshTab::_frame(EditorContext& ctx)
{
    if (_framed || !ctx.renderer) return;
    if (const LMesh* mesh = ctx.renderer->geometry.get(ctx.renderer->geometry.find(guid))) {
        orbit.frame(glm::vec3(mesh->bounds_sphere), mesh->bounds_sphere.w);
        _framed = true;
    }
}

void MeshTab::_refresh_stats(EditorContext& ctx)
{
    const double now = ImGui::GetTime();
    if (now - _stats_poll < STATS_POLL_S || !ctx.project) return;
    _stats_poll = now;

    const std::filesystem::path bin = ctx.project->content_path(guid);
    std::error_code ec;
    const std::filesystem::file_time_type t = std::filesystem::last_write_time(bin, ec);
    if (ec || t == _stats_time) return;
    _stats_time = t;
    _framed = false;
    if (!lmesh_read_stats(bin, mesh_stats)) mesh_stats = {};
}

}
