#include <editor/documents/mesh/mesh_document.h>
#include <core/rendering/debug_draw.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/scene/components.h>
#include <core/scene/scene.h>
#include <editor/documents/mesh/panels/mesh_details_panel.h>
#include <editor/viewport/viewport.h>
#include <core/physics/collision_preview.h>
#include <editor/assets/import/mesh_cooker.h>
#include <editor/assets/import/dynamics_baker.h>
#include <editor/assets/import/skeleton_cooker.h>
#include <core/animation/skinning.h>
#include <core/base/hash.h>
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
        _draw_collision(r_draw);
        if (!rig_preview.skinned) return;
        _draw_bones(r_draw);
        if (_simulating) _sim.draw(r_draw, true, true);
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
    if (shapes_dirty()) {
        if (CollisionCooker::write_shapes(ctx, path, collision.edit) != OK) return FAILED;
        collision.settings = collision.edit;
        collision.has = !collision.edit.shapes.empty();
        _collision_poll = -1.0e9;
    }
    if (dynamics_dirty()) {
        const bool rebake = cloth_rebake();
        const std::vector<SkeletonCloth>& saved = rig_preview.skeleton_data().cloths;
        bool slots_changed = saved.size() != cloths.size();
        for (size_t i = 0; !slots_changed && i < cloths.size(); i++) slots_changed = saved[i].slot != cloths[i].slot;
        LSkeleton skeleton = rig_preview.skeleton_data();
        skeleton.chains = chains;
        skeleton.cloths = cloths;
        const std::filesystem::path bin = ctx.project->content_path(guid);
        if (SkeletonCooker::save_dynamics(path, bin, skeleton) != OK) return FAILED;
        if (rebake && dynamics_rebake_cloths(bin, skeleton) != OK) return FAILED;
        if (ctx.renderer) {
            if (rebake) {
                ctx.renderer->geometry.unload(guid);
                ctx.renderer->geometry.load(guid, bin);
            }
            ctx.renderer->skeletons.load_mesh(guid, bin);
        }
        rig_preview.invalidate();
        _sim = {};
        if (slots_changed && collision_settings_generated(collision.settings)) {
            CollisionCooker::reimport(ctx, path, collision.settings);
            _collision_poll = -1.0e9;
        }
    }
    _refresh_title();
    return OK;
}

void MeshDocument::destroy(EditorContext& ctx)
{
    if (dynamics_dirty() && ctx.renderer && ctx.project) ctx.renderer->skeletons.load_mesh(guid, ctx.project->content_path(guid));
    AssetDocument::destroy(ctx);
}

void MeshDocument::on_update(EditorContext& ctx, float p_dt)
{
    _draw_layout(ctx);
    _edit_selected_shape(ctx);
    _update_orbit();
    _refresh_stats(ctx);
    if (ImGui::GetTime() - _collision_poll >= STATS_POLL_S) {
        _collision_poll = ImGui::GetTime();
        _refresh_collision(ctx);
    }
    rig_preview.refresh(ctx, guid);
    _sync_dynamics();
    _pick_bone(ctx);
    _simulate_preview(p_dt, ctx.scene ? ctx.scene->physics : PhysicsSettings{});
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
    collision.has = CollisionCooker::has_collision(path);

    if (!collision.has) {
        collision.loaded = false;
        collision.edges.clear();
        collision.time = {};
    } else {
        const std::filesystem::path bin = ctx.project->content_path(guid);
        std::error_code ec;
        const std::filesystem::file_time_type t = std::filesystem::last_write_time(bin, ec);
        if (!ec && t != collision.time) {
            collision.time = t;
            std::vector<uint8_t> payload;
            collision.loaded = lmesh_read_collision(bin, payload) && collision.data.from_payload(payload.data(), payload.size()) == Error::OK;
            collision.edges.clear();
            if (collision.loaded) collision_edges(collision.data, collision.edges);
        }
    }

    CollisionSettings legacy = saved.collision;
    if (!collision.has && legacy.type != CollisionType::SHAPES) legacy.type = CollisionType::NONE;
    CollisionSettings current = collision_settings_upgrade(legacy);
    if (collision.loaded && collision.data.primitive() && current.type == collision.data.type) {
        current.type = CollisionType::SHAPES;
        current.shapes = { collision_shape_from(collision.data) };
    }
    if (!(current == collision.settings)) {
        if (collision.edit == collision.settings) collision.edit = current;
        collision.settings = current;
    }
    if (selected_shape >= (int32_t)collision.edit.shapes.size()) selected_shape = -1;
}

void MeshDocument::fit_shape(CollisionShapeSettings& r_shape) const
{
    const vec3 size = max(mesh_stats.size, vec3(CollisionCooker::MIN_PRIMITIVE_SIZE));
    r_shape.position = mesh_stats.bounds_min + size * 0.5f;
    r_shape.rotation = vec3(0.0f);
    r_shape.half_extents = size * 0.5f;
    const float round = std::max(size.x, size.z) * 0.5f;
    switch (r_shape.type) {
        case CollisionType::SPHERE:
            r_shape.radius = length(size) * 0.5f;
            break;
        case CollisionType::CAPSULE:
            r_shape.radius = round;
            r_shape.half_height = std::max(size.y * 0.5f - round, CollisionCooker::MIN_PRIMITIVE_SIZE);
            break;
        case CollisionType::CYLINDER:
            r_shape.radius = round;
            r_shape.top_radius = round;
            r_shape.half_height = size.y * 0.5f;
            break;
        case CollisionType::TAPERED_CYLINDER:
            r_shape.radius = round;
            r_shape.top_radius = round * 0.5f;
            r_shape.half_height = size.y * 0.5f;
            break;
        case CollisionType::TAPERED_CAPSULE:
            r_shape.radius = round;
            r_shape.top_radius = round * 0.5f;
            r_shape.half_height = std::max(size.y * 0.5f - round, CollisionCooker::MIN_PRIMITIVE_SIZE);
            break;
        default:
            break;
    }
}

bool MeshDocument::source_used(CollisionType p_type, int32_t p_slot, int32_t p_except) const
{
    for (int32_t i = 0; i < (int32_t)collision.edit.shapes.size(); i++) {
        const CollisionShapeSettings& s = collision.edit.shapes[i];
        if (i != p_except && s.type == p_type && s.slot == p_slot) return true;
    }
    return false;
}

int32_t MeshDocument::free_source(CollisionType p_type) const
{
    if (!source_used(p_type, COLLISION_ALL_SLOTS)) return COLLISION_ALL_SLOTS;
    for (int32_t s = 0; s < (int32_t)mesh_stats.slots; s++) {
        if (!source_used(p_type, s)) return s;
    }
    return INT32_MIN;
}

void MeshDocument::add_shape(CollisionType p_type)
{
    CollisionShapeSettings s;
    if (p_type == CollisionType::MESH || p_type == CollisionType::HULL) {
        const int32_t slot = free_source(p_type);
        if (slot == INT32_MIN) return;
        s = collision_generated_shape(p_type, slot);
    } else {
        s.type = p_type;
        s.name = collision_type_label(p_type);
        fit_shape(s);
    }
    collision.edit.type = CollisionType::SHAPES;
    collision.edit.shapes.push_back(std::move(s));
    selected_shape = (int32_t)collision.edit.shapes.size() - 1;
}

void MeshDocument::_edit_selected_shape(EditorContext& ctx)
{
    if (!shapes_mode() || selected_shape < 0 || selected_shape >= (int32_t)collision.edit.shapes.size() || collision.edit.shapes[selected_shape].generated() || locked(ctx)) {
        viewport.shape_gizmo.reset();
        return;
    }
    CollisionShapeSettings& s = collision.edit.shapes[selected_shape];
    const ShapeEdit edit{ .kind = shape_kind(s.type), .position = &s.position, .rotation = &s.rotation, .half_extents = &s.half_extents, .radius = &s.radius, .half_height = &s.half_height, .top_radius = &s.top_radius };
    bool changed = false;
    viewport.edit_shape(ctx, edit, changed);
}

void MeshDocument::_draw_collision(DebugDraw& r_draw) const
{
    const DebugDraw::Style style{ .color = DebugDraw::GREEN, .category = DebugCategory::COLLISION };
    const DebugDraw::Style selected{ .color = DebugDraw::YELLOW, .category = DebugCategory::COLLISION };
    const std::vector<CollisionShapeSettings>& shapes = collision.edit.shapes;
    if (collision.loaded) {
        const LCollision& d = collision.data;
        int32_t generated = 0, selected_part = -1;
        for (int32_t i = 0; i < (int32_t)shapes.size(); i++) {
            if (!shapes[i].generated()) continue;
            if (i == selected_shape) selected_part = generated;
            generated++;
        }
        const LCollisionPart* part = selected_part >= 0 && generated == (int32_t)d.parts.size() && !shapes_dirty() ? &d.parts[selected_part] : nullptr;
        for (size_t i = 0; i + 1 < collision.edges.size(); i += 2) {
            const uint32_t a = collision.edges[i], b = collision.edges[i + 1];
            const bool hot = part && a >= part->vertex_first && a < part->vertex_first + part->vertex_count;
            r_draw.line(d.vertices[a], d.vertices[b], hot ? selected : style);
        }
    }
    for (size_t i = 0; i < shapes.size(); i++) {
        if (shapes[i].generated()) continue;
        debug_draw_lcollision_shape(r_draw, mat4(1.0f), collision_shape_cook(shapes[i]), (int32_t)i == selected_shape ? selected : style);
    }
}

void MeshDocument::_sync_dynamics()
{
    if (_dynamics_version == rig_preview.version) return;
    _dynamics_version = rig_preview.version;
    chains = rig_preview.skeleton_data().chains;
    cloths = rig_preview.skeleton_data().cloths;
    if (selected_bone >= (int32_t)rig_preview.skeleton_data().joint_count()) selected_bone = -1;
}

bool MeshDocument::cloth_rebake() const
{
    const std::vector<SkeletonCloth>& saved = rig_preview.skeleton_data().cloths;
    if (saved.size() != cloths.size()) return true;
    for (size_t i = 0; i < cloths.size(); i++) {
        if (saved[i].slot != cloths[i].slot || saved[i].resolution != cloths[i].resolution) return true;
    }
    return false;
}

void MeshDocument::push_dynamics(EditorContext& ctx)
{
    if (!ctx.renderer || !rig_preview.skinned) return;
    auto it = ctx.renderer->skeletons.entries.find(rig_preview.rig.skeleton);
    if (it == ctx.renderer->skeletons.entries.end()) return;
    LSkeleton& live = it->second.skeleton;
    bool topology = live.chains.size() != chains.size() || live.cloths.size() != cloths.size();
    for (size_t i = 0; !topology && i < chains.size(); i++) topology = live.chains[i].root != chains[i].root;
    for (size_t i = 0; !topology && i < cloths.size(); i++) topology = live.cloths[i].slot != cloths[i].slot;
    live.chains = chains;
    live.cloths = cloths;
    if (topology) ctx.renderer->skeletons.version++;
}

void MeshDocument::_pick_bone(EditorContext& ctx)
{
    if (!viewport.picked || !rig_preview.skinned || rig_preview._global.empty() || locked(ctx)) return;
    std::vector<vec3> joints(rig_preview._global.size());
    for (size_t i = 0; i < joints.size(); i++) joints[i] = vec3(rig_preview._global[i][3]);
    selected_bone = viewport.pick_point(ctx, joints.data(), joints.size(), BONE_PICK_PX);}

void MeshDocument::_draw_bones(DebugDraw& r_draw) const
{
    const LSkeleton& skeleton = rig_preview.skeleton_data();
    if (skeleton.joint_count() == 0) return;
    std::vector<float> weights(skeleton.joint_count(), 0.0f);
    std::vector<uint8_t> branch;
    auto mark = [&](int32_t p_root) {
        skeleton.branch(p_root, branch);
        for (size_t j = 0; j < branch.size() && j < weights.size(); j++) {
            if (branch[j]) weights[j] = std::max(weights[j], 0.45f);
        }
    };
    for (const SkeletonChain& c : chains) mark(c.root);
    if (selected_bone >= 0 && selected_bone < (int32_t)weights.size()) weights[selected_bone] = 1.0f;
    rig_preview.draw_bones(r_draw, weights.data());
}

void MeshDocument::_simulate_preview(float p_dt, const PhysicsSettings& p_physics)
{
    const bool active = rig_preview.skinned && !rig_preview._global.empty() && (!chains.empty() || !cloths.empty());
    if (!active) {
        if (_simulating) {
            _simulating = false;
            _sim = {};
            _applied_version = 0;
            rig_preview.pose(rig_preview.skeleton_data().rest.data());
        }
        return;
    }
    LSkeleton skeleton = rig_preview.skeleton_data();
    skeleton.chains = chains;
    skeleton.cloths = cloths;
    uint64_t key = rig_preview.version;
    for (const SkeletonChain& c : chains) key = hash_combine(key, (uint64_t)c.root);
    for (const SkeletonCloth& c : cloths) key = hash_combine(key, (uint64_t)c.slot ^ 0x9e3779b97f4a7c15ull);
    const mat4 model(1.0f);
    std::vector<JointTransform> local = skeleton.rest;
    std::vector<mat4>& global = rig_preview._global;
    skeleton.globals(local.data(), global.data());
    static const LLocomotion DEFAULT_BODY;
    _sim.bind(key, skeleton, rig_preview.rig, DEFAULT_BODY);
    _sim.update_colliders(global, model, 0.0f);
    if (p_physics.chains) _sim.simulate_chains(skeleton, model, p_dt, p_physics.gravity, local.data(), global);
    else _sim.follow_chains(model, global);
    rig_skin(global, rig_preview.rig, rig_preview._map, rig_preview._skin);
    _sim.simulate_cloth(skeleton, rig_preview.rig, model, p_dt, p_physics.gravity, p_physics.cloth, rig_preview._skin);
    _sim.finish(model);
    _simulating = true;
    rig_preview.apply(preview, _preview_entity);
}

}
