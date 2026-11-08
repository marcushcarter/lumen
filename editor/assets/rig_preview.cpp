#include <editor/assets/rig_preview.h>
#include <core/animation/skinning.h>
#include <core/project/project.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/world/camera.h>
#include <core/world/components.h>
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

void RigPreview::refresh(EditorContext& ctx, Guid p_mesh, bool p_keep_skeleton)
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
            if (new_mesh || !p_keep_skeleton) skeleton = rig.skeleton;
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
    for (int32_t s : _map) {
        if (s == SKELETON_NO_JOINT) missing_joints++;
    }
}

Guid RigPreview::find_mesh(EditorContext& ctx, Guid p_skeleton)
{
    if (!p_skeleton.valid() || !ctx.assets) return {};
    for (const auto& [mesh, e] : ctx.assets->entries) {
        if (e.type != AssetType::MESH) continue;
        toml::table tbl;
        try { tbl = toml::parse_file(e.path.string()); }
        catch (const toml::parse_error&) { continue; }
        if (Guid::from_string(tbl[asset_type_section(AssetType::MESH)]["skeleton"].value_or(std::string())) == p_skeleton) return mesh;
    }
    return {};
}

void RigPreview::pose(const JointTransform* p_local)
{
    if (_global.empty()) return;
    _skeleton.globals(p_local, _global.data());
    rig_skin(_global, rig, _map, _skin);
}

void RigPreview::apply(World& r_world, Entity p_entity) const
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

void RigPreview::draw_controls()
{
    if (!skinned) return;
    ImGui::Checkbox("Bones", &show_bones);
}

void RigPreview::draw_bones(ImDrawList* p_draw_list, ImVec2 p_pos, ImVec2 p_size, const Camera& p_camera, const float* p_weights) const
{
    if (!show_bones || _global.empty() || p_size.x < 1.0f || p_size.y < 1.0f) return;

    const UiStyle& ui = ui_style();
    const mat4 view_proj = p_camera.proj(p_size.x / p_size.y) * p_camera.view();
    auto project = [&](const mat4& p_global, ImVec2& r_screen) {
        const vec4 clip = view_proj * vec4(vec3(p_global[3]), 1.0f);
        if (clip.w <= p_camera.near_z) return false;
        r_screen = ImVec2(p_pos.x + (clip.x / clip.w * 0.5f + 0.5f) * p_size.x, p_pos.y + (0.5f - clip.y / clip.w * 0.5f) * p_size.y);
        return true;
    };

    const ImVec4 off = ImGui::ColorConvertU32ToFloat4(ui.color.text_dim);
    const ImVec4 on = ImGui::ColorConvertU32ToFloat4(ui.color.asset_overlay);
    for (size_t i = 0; i < _global.size(); ++i) {
        ImVec2 joint;
        if (!project(_global[i], joint)) continue;
        const ImU32 color = p_weights ? ImGui::ColorConvertFloat4ToU32(ImLerp(off, on, p_weights[i])) : ui.color.asset_skeleton;
        const int32_t parent = _skeleton.parents[i];
        ImVec2 from;
        if (parent != SKELETON_NO_JOINT && project(_global[parent], from)) p_draw_list->AddLine(from, joint, color, ui.bones.bone_thickness);
        p_draw_list->AddCircleFilled(joint, ui.bones.joint_radius, color);
    }
}

}
