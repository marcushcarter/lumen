#pragma once
#include <editor/tabs/editor_tab.h>
#include <core/assets/lmodel.h>
#include <editor/layout/editor_layout.h>
#include <editor/viewport/orbit_camera.h>
#include <core/world/world.h>
#include <core/assets/lmesh.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <string>

namespace lumen {

struct ModelTab : EditorTab
{
    static constexpr TabKind KIND = TabKind::MODEL;
    static constexpr const char* ICON = ICON_FA_CUBES;
    static constexpr double STATS_POLL_S = 1.0;

    LModel model;
    LModel saved;
    std::filesystem::path path;
    EditorLayout layout;
    OrbitCamera orbit;
    World preview;
    Guid _preview_model;
    Guid _preview_mesh;
    Guid _framed_mesh;
    std::string _title;

    LMeshStats mesh_stats;
    Guid _stats_mesh;
    std::filesystem::file_time_type _stats_time{};
    double _stats_poll = -1.0e9;

    const char* title() const override { return _title.empty() ? "Model" : _title.c_str(); }
    bool dirty() const override { return !(model.mesh == saved.mesh); }
    const Camera* camera() const override { return &orbit.camera; }
    const World* scene() const override { return &preview; }

    Error create(EditorContext& ctx) override;
    void destroy(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_tick(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;

    void _refresh_title();
    void _sync_preview(EditorContext& ctx);
    void _refresh_stats(EditorContext& ctx);
};

}