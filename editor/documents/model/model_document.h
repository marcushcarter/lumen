#pragma once
#include <core/assets/lmesh.h>
#include <core/assets/lmodel.h>
#include <editor/assets/rig_preview.h>
#include <editor/documents/asset_document.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <vector>

namespace lumen {

struct ModelDocument : AssetDocument
{
    static constexpr DocumentKind KIND = DocumentKind::MODEL;
    static constexpr const char* ICON = ICON_FA_CUBES;
    static constexpr double STATS_POLL_S = 1.0;

    LModel model;
    LModel saved;
    Guid _preview_mesh;
    std::vector<Guid> _preview_materials;
    Guid _framed_mesh;
    RigPreview rig_preview;
    uint64_t _framed_version = 0;
    uint64_t _applied_version = 0;

    LMeshStats mesh_stats;
    Guid _stats_mesh;
    std::filesystem::file_time_type _stats_time{};
    double _stats_poll = -1.0e9;

    const char* _type_name() const override { return "Model"; }
    void _sync_preview(EditorContext& ctx);
    void _refresh_stats(EditorContext& ctx);

    bool dirty() const override { return model.mesh != saved.mesh || model.materials != saved.materials; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
