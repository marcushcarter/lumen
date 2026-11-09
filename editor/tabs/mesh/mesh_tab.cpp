#include <editor/tabs/mesh/mesh_tab.h>
#include <core/rendering/debug_draw.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <editor/tabs/mesh/panels/mesh_details_panel.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <toml++/toml.hpp>
#include <algorithm>
#include <sstream>

namespace lumen {

Error MeshTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    _refresh_title();

    _create_layout(ctx, "MeshHost");
    layout.add_panel<MeshDetailsPanel>()->tab = this;
    layout.viewport.debug_lines = [this] {
        rig_preview.draw_bones(debug_draw());
        _draw_collision();
    };
    layout.viewport.show_collision = false;

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
    layout.viewport.has_bones = rig_preview.skinned;
    layout.viewport.has_collision = collision.loaded;
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

void MeshTab::_refresh_collision(EditorContext& ctx)
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

    std::vector<uint64_t> keys;
    keys.reserve(collision.data.indices.size());
    for (size_t i = 0; i + 2 < collision.data.indices.size(); i += 3) {
        for (int e = 0; e < 3; e++) {
            const uint32_t a = collision.data.indices[i + e];
            const uint32_t b = collision.data.indices[i + (e + 1) % 3];
            keys.push_back(((uint64_t)std::min(a, b) << 32) | std::max(a, b));
        }
    }
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    collision.edges.reserve(keys.size() * 2);
    for (uint64_t k : keys) {
        collision.edges.push_back((uint32_t)(k >> 32));
        collision.edges.push_back((uint32_t)k);
    }
}

void MeshTab::_draw_collision() const
{
    if (!collision.loaded) return;
    const LCollision& c = collision.data;
    const DebugDraw::Style style{ .color = DebugDraw::GREEN, .category = DebugCategory::COLLISION };
    DebugDraw& draw = debug_draw();
    switch (c.type) {
        case CollisionType::BOX:
            draw.box(translate(mat4(1.0f), c.center) * mat4_cast(c.rotation), c.half_extents, style);
            return;
        case CollisionType::SPHERE:
            draw.sphere(c.center, c.radius, style);
            return;
        case CollisionType::CAPSULE: {
            const vec3 axis = c.rotation * vec3(0.0f, 1.0f, 0.0f);
            draw.capsule(c.center - axis * c.half_height, c.center + axis * c.half_height, c.radius, style);
            return;
        }
        default:
            for (size_t i = 0; i + 1 < collision.edges.size(); i += 2) draw.line(c.vertices[collision.edges[i]], c.vertices[collision.edges[i + 1]], style);
            return;
    }
}

}
