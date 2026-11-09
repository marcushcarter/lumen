#pragma once
#include <core/assets/lmesh.h>
#include <core/assets/lcollision.h>
#include <editor/assets/import/collision_cooker.h>
#include <editor/assets/rig_preview.h>
#include <editor/tabs/asset_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <vector>

namespace lumen {

struct MeshTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::MESH;
    static constexpr const char* ICON = ICON_FA_CUBE;
    static constexpr double STATS_POLL_S = 1.0;

    LMeshStats mesh_stats;
    RigPreview rig_preview;

    struct CollisionView {
        bool has = false;
        bool generate = true;
        CollisionSettings settings;
        CollisionSettings edit;
        LCollision data;
        bool loaded = false;
        std::vector<uint32_t> edges;
        std::filesystem::file_time_type time{};
    };
    CollisionView collision;

    std::filesystem::file_time_type _stats_time{};
    double _stats_poll = -1.0e9;
    double _collision_poll = -1.0e9;
    bool _framed = false;
    uint64_t _framed_version = 0;
    uint64_t _applied_version = 0;

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_mesh; }
    void _refresh_stats(EditorContext& ctx);
    void _refresh_collision(EditorContext& ctx);
    void _draw_collision() const;
    void _frame(EditorContext& ctx);

    const char* title() const override { return _title.empty() ? "Mesh" : _title.c_str(); }
    bool dirty() const override { return rig_preview.skinned && rig_preview.skeleton != rig_preview.rig.skeleton; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
