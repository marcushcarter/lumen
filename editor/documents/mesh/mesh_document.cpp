#include <editor/documents/mesh/mesh_document.h>
#include <core/rendering/debug_draw.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/scene/components.h>
#include <editor/documents/mesh/panels/mesh_details_panel.h>
#include <editor/viewport/viewport.h>
#include <core/physics/collision_preview.h>
#include <editor/assets/import/mesh_cooker.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <toml++/toml.hpp>
#include <algorithm>
#include <sstream>

namespace lumen {

Error MeshDocument::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    _refresh_title();

    _create_layout();
    layout.add_panel<MeshDetailsPanel>()->tab = this;
    viewport.debug_lines = [this](DebugDraw& r_draw) {
        rig_preview.draw_bones(r_draw);
        _draw_collision(r_draw);
    };

    _create_preview();
    preview.remove<StaticTag>(_preview_entity);
    preview.add<PoseComponent>(_preview_entity);
    ctx.renderer->models.set(_preview_model, guid, {});

    orbit.update();
    return OK;
}

Error MeshDocument::save(EditorContext& ctx)
{
    using enum Error;
    if (!dirty() || !ctx.project) return OK;
    if (!lmesh_set_skeleton(ctx.project->content_path(guid), rig_preview.skeleton)) {
        log_write("Mesh: failed to set the skeleton of '%s'.", path.string().c_str());
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

void MeshDocument::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    _draw_layout(ctx);
    _update_orbit();
    _refresh_stats(ctx);
    if (ImGui::GetTime() - _collision_poll >= STATS_POLL_S) {
        _collision_poll = ImGui::GetTime();
        _refresh_collision(ctx);
    }
    rig_preview.refresh(ctx, guid, dirty());
    if (rig_preview.version != _applied_version) {
        rig_preview.apply(preview, _preview_entity);
        _applied_version = rig_preview.version;
    }
    _frame(ctx);
    orbit.update();
}

void MeshDocument::_frame(EditorContext& ctx)
{
    if ((_framed && _framed_version == rig_preview.version) || !ctx.renderer) return;
    vec3 center;
    float radius;
    if (!rig_preview.bounds(ctx.renderer->geometry, guid, center, radius)) return;
    orbit.frame(center, radius);
    _framed = true;
    _framed_version = rig_preview.version;
}

void MeshDocument::_refresh_stats(EditorContext& ctx)
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

void MeshDocument::_refresh_collision(EditorContext& ctx)
{
    if (!ctx.project || path.empty()) return;
    MeshCooker::CookSettings saved;
    std::filesystem::path saved_source;
    MeshCooker::load_settings(path, saved, saved_source);
    collision.generate = saved.generate_collision;
    collision.has = CollisionCooker::has_collision(path);
    if (!(saved.collision == collision.settings)) {
        if (collision.edit == collision.settings) collision.edit = saved.collision;
        collision.settings = saved.collision;
    }
    if (!collision.has) {
        collision.loaded = false;
        collision.edges.clear();
        collision.time = {};
        return;
    }

    const std::filesystem::path bin = ctx.project->content_path(guid);
    std::error_code ec;
    const std::filesystem::file_time_type t = std::filesystem::last_write_time(bin, ec);
    if (ec || t == collision.time) return;
    collision.time = t;
    std::vector<uint8_t> payload;
    collision.loaded = lmesh_read_collision(bin, payload) && collision.data.from_payload(payload.data(), payload.size()) == Error::OK;
    collision.edges.clear();
    if (!collision.loaded) return;

    collision_edges(collision.data, collision.edges);
}

void MeshDocument::_draw_collision(DebugDraw& r_draw) const
{
    if (!collision.loaded) return;
    const DebugDraw::Style style{ .color = DebugDraw::GREEN, .category = DebugCategory::COLLISION };
    const bool meshes = (view_overlay_kinds(viewport_view().overlay) & ENTITY_DEBUG_MESH_COLLISION) != 0;
    debug_draw_lcollision(r_draw, mat4(1.0f), collision.data, collision.edges, style, meshes);
}

}
