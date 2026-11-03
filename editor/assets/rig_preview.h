#pragma once
#include <core/assets/lmesh.h>
#include <core/assets/lskeleton.h>
#include <core/world/world.h>
#include <imgui.h>
#include <filesystem>
#include <vector>

namespace lumen {

struct Camera;
struct EditorContext;
struct GeometryPool;
struct OverlayBar;

struct RigPreview
{
    static constexpr double POLL_S = 1.0;

    LMeshRig rig;
    bool skinned = false;
    Guid skeleton;
    bool show_bones = true;
    uint32_t missing_joints = 0;
    uint64_t version = 0;

    LSkeleton _skeleton;
    std::vector<mat4> _global;
    std::vector<int32_t> _map;
    std::vector<mat4> _skin;
    Guid _mesh;
    Guid _skeleton_loaded;
    std::filesystem::file_time_type _mesh_time{};
    double _poll = -1.0e9;
    bool _stale = true;

    void refresh(EditorContext& ctx, Guid p_mesh, bool p_keep_skeleton);
    void invalidate() { _mesh = Guid{}; }
    const LSkeleton& skeleton_data() const { return _skeleton; }
    void pose(const JointTransform* p_local);
    void apply(World& r_world, Entity p_entity) const;
    void draw_bones(ImDrawList* p_draw_list, ImVec2 p_pos, ImVec2 p_size, const Camera& p_camera) const;
    void draw_controls(OverlayBar& r_bar);
    bool bounds(const GeometryPool& p_geometry, Guid p_mesh, vec3& r_center, float& r_radius) const;
    void _load_skeleton(EditorContext& ctx);
};

}
