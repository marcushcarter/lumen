#pragma once
#include <core/assets/lmesh.h>
#include <core/assets/lmodel.h>
#include <editor/tabs/asset_tab.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <vector>

namespace lumen {

struct ModelTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::MODEL;
    static constexpr const char* ICON = ICON_FA_CUBES;
    static constexpr double STATS_POLL_S = 1.0;

    LModel model;
    LModel saved;
    Guid _preview_mesh;
    std::vector<Guid> _preview_materials;
    Guid _framed_mesh;

    LMeshStats mesh_stats;
    Guid _stats_mesh;
    std::filesystem::file_time_type _stats_time{};
    double _stats_poll = -1.0e9;

    const char* _icon() const override { return ICON; }
    void _sync_preview(EditorContext& ctx);
    void _refresh_stats(EditorContext& ctx);

    const char* title() const override { return _title.empty() ? "Model" : _title.c_str(); }
    bool dirty() const override { return model.mesh != saved.mesh || model.materials != saved.materials; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
