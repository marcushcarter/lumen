#include <editor/assets/rig_preview.h>
#include <core/animation/skinning.h>
#include <core/animation/bone_debug.h>
#include <core/project/project.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/scene/camera.h>
#include <core/scene/components.h>
#include <editor/application/editor_context.h>
#include <editor/assets/asset_registry.h>
#include <core/assets/asset_common.h>
#include <toml++/toml.hpp>
#include <editor/ui/ui_style.h>
#include <editor/viewport/overlay_bar.h>
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <cfloat>

namespace lumen {

void RigPreview::refresh(EditorContext& ctx, Guid p_mesh)
{
    if (!ctx.project) return;
    const bool new_mesh = p_mesh != _mesh;
    const double now = ImGui::GetTime();
    if (new_mesh || now - _poll >= POLL_S) {
        _poll = now;
        const std::filesystem::path bin = ctx.project->content_path(p_mesh);
        std::error_code ec;
        const std::filesystem::file_time_type t = std::filesystem::last_write_time(bin, ec);
        if (new_mesh || (!ec && t != _mesh_time)) {
            _mesh = p_mesh;
            _mesh_time = ec ? std::filesystem::file_time_type{} : t;
            skinned = p_mesh.valid() && lmesh_read_rig(bin, rig);
            skeleton = rig.skeleton;
            _stale = true;
        }
    }
    if (_stale || skeleton != _skeleton_loaded) _load_skeleton(ctx);
}

void RigPreview::_load_skeleton(EditorContext& ctx)
{
    _stale = false;
    _skeleton_loaded = skeleton;
    _skeleton = {};
    _global.clear();
    _map.clear();
    _skin.clear();
    missing_joints = 0;
    version++;
    if (!skinned || !skeleton.valid()) return;

    if (_skeleton.load_cooked(ctx.project->content_path(skeleton)) != Error::OK) {
        log_write("Rig Preview: failed to load skeleton %s.", skeleton.to_string().c_str());
        _skeleton = {};
        return;
    }
    _global.resize(_skeleton.joint_count());
    _skeleton.globals(_skeleton.rest.data(), _global.data());
    rig_joint_map(_skeleton, rig, _map);
    rig_skin(_global, rig, _map, _skin);
    for (size_t j = 0; j < _map.size() && j < rig.joints.size(); j++) {
        if (_map[j] == SKELETON_NO_JOINT && !rig.joints[j].starts_with("__cloth")) missing_joints++;
    }
}

Guid RigPreview::find_mesh(EditorContext& ctx, Guid p_skeleton)
{
    if (!p_skeleton.valid() || !ctx.assets) return {};
    const auto it = ctx.assets->entries.find(p_skeleton);
    return it != ctx.assets->entries.end() && it->second.type == AssetType::MESH ? p_skeleton : Guid{};
}

void RigPreview::pose(const JointTransform* p_local)
{
    if (_global.empty()) return;
    _skeleton.globals(p_local, _global.data());
    rig_skin(_global, rig, _map, _skin);
}

void RigPreview::apply(Scene& r_world, Entity p_entity) const
{
    PoseComponent* pose = r_world.try_get<PoseComponent>(p_entity);
    if (!pose) return;
    pose->skin = _skin;
}

bool RigPreview::bounds(const GeometryPool& p_geometry, Guid p_mesh, vec3& r_center, float& r_radius) const
{
    const uint32_t id = p_geometry.find(p_mesh);
    const LMesh* mesh = p_geometry.get(id);
    if (!mesh) return false;
    r_center = vec3(mesh->bounds_sphere);
    r_radius = mesh->bounds_sphere.w;

    if (_skin.empty() || !p_geometry.joint_boxes(id)) return true;
    vec3 lo, hi;
    p_geometry.posed_box(id, _skin.data(), (uint32_t)_skin.size(), lo, hi);
    r_center = (lo + hi) * 0.5f;
    r_radius = length(hi - lo) * 0.5f;
    return true;
}

void RigPreview::draw_bones(DebugDraw& r_draw, const float* p_weights) const
{
    if (_global.empty()) return;

    const UiStyle& ui = ui_style();
    const ImVec4 off = ImGui::ColorConvertU32ToFloat4(ui.color.text_dim);
    const ImVec4 on = ImGui::ColorConvertU32ToFloat4(ui.color.asset_overlay);
    for (size_t i = 0; i < _global.size(); ++i) {
        const uint32_t color = p_weights ? ImGui::ColorConvertFloat4ToU32(ImLerp(off, on, p_weights[i])) : DebugDraw::WHITE;
        debug_draw_joint(r_draw, vec3(_global[i][3]), color);
        const int32_t parent = _skeleton.parents[i];
        if (parent == SKELETON_NO_JOINT || parent >= (int32_t)_global.size()) continue;
        debug_draw_bone(r_draw, vec3(_global[parent][3]), vec3(_global[i][3]), color);
    }
}

}
