#include <editor/tabs/mesh/mesh_tab.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <editor/tabs/mesh/panels/mesh_details_panel.h>
#include <imgui.h>
#include <toml++/toml.hpp>
#include <sstream>

namespace lumen {

Error MeshTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    _refresh_title();

    _create_layout(ctx, "MeshHost");
    layout.add_panel<MeshDetailsPanel>()->tab = this;
    layout.viewport.overlay = [this](ImDrawList* p_draw_list, ImVec2 p_pos, ImVec2 p_size) { rig_preview.draw_bones(p_draw_list, p_pos, p_size, orbit.camera); };
    layout.viewport.options_controls = [this] { rig_preview.draw_controls(); };

    _create_preview();
    preview.remove<StaticTag>(_preview_entity);
    preview.add<PoseComponent>(_preview_entity);
    ctx.renderer->models.set(_preview_model, guid, {});

    orbit.update();
    return OK;
}

Error MeshTab::save(EditorContext& ctx)
{
    using enum Error;
    if (!dirty() || !ctx.project) return OK;
    if (!lmesh_set_skeleton(ctx.project->content_path(guid), rig_preview.skeleton)) {
        log_write("Mesh Tab: failed to set the skeleton of '%s'.", path.string().c_str());
        return FAILED;
    }

    toml::table tbl;
    try { tbl = toml::parse_file(path.string()); }
    catch (const toml::parse_error&) { return FAILED; }
    if (toml::table* section = tbl[asset_type_section(AssetType::MESH)].as_table()) section->insert_or_assign("skeleton", rig_preview.skeleton.to_string());
    std::ostringstream ss;
    ss << tbl;
    if (!write_file_atomic(path, ss.str())) return FAILED;

    rig_preview.rig.skeleton = rig_preview.skeleton;
    if (ctx.renderer) ctx.renderer->geometry.set_skeleton(guid, rig_preview.skeleton);
    _refresh_title();
    return OK;
}

void MeshTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    _draw_layout(ctx);
    _update_orbit();
    _refresh_stats(ctx);
    rig_preview.refresh(ctx, guid, dirty());
    if (rig_preview.version != _applied_version) {
        rig_preview.apply(preview, _preview_entity);
        _applied_version = rig_preview.version;
    }
    _frame(ctx);
    orbit.update();
}

void MeshTab::_frame(EditorContext& ctx)
{
    if ((_framed && _framed_version == rig_preview.version) || !ctx.renderer) return;
    vec3 center;
    float radius;
    if (!rig_preview.bounds(ctx.renderer->geometry, guid, center, radius)) return;
    orbit.frame(center, radius);
    _framed = true;
    _framed_version = rig_preview.version;
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
