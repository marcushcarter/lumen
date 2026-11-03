#pragma once
#include <core/assets/lmesh.h>
#include <editor/tabs/asset_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <filesystem>

namespace lumen {

struct MeshTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::MESH;
    static constexpr const char* ICON = ICON_FA_CUBE;
    static constexpr double STATS_POLL_S = 1.0;

    LMeshStats mesh_stats;
    std::filesystem::file_time_type _stats_time{};
    double _stats_poll = -1.0e9;
    bool _framed = false;

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_mesh; }
    void _refresh_stats(EditorContext& ctx);
    void _frame(EditorContext& ctx);

    const char* title() const override { return _title.empty() ? "Mesh" : _title.c_str(); }
    Error create(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
