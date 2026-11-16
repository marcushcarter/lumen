#pragma once
#include <core/assets/lmesh.h>
#include <core/assets/lcollision.h>
#include <editor/assets/import/collision_cooker.h>
#include <editor/assets/rig_preview.h>
#include <editor/documents/asset_document.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <vector>

namespace lumen {

struct MeshDocument : AssetDocument
{
    static constexpr DocumentKind KIND = DocumentKind::MESH;
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
    int32_t selected_shape = -1;

    std::filesystem::file_time_type _stats_time{};
    double _stats_poll = -1.0e9;
    double _collision_poll = -1.0e9;
    bool _framed = false;
    uint64_t _framed_version = 0;
    uint64_t _applied_version = 0;

    const char* _type_name() const override { return "Mesh"; }
    void _refresh_stats(EditorContext& ctx);
    void _refresh_collision(EditorContext& ctx);
    void _draw_collision(DebugDraw& r_draw) const;
    void _edit_selected_shape(EditorContext& ctx);

    bool shapes_mode() const { return collision.edit.type == CollisionType::SHAPES; }
    bool shapes_dirty() const { return shapes_mode() && (!(collision.edit == collision.settings) || (!collision.edit.shapes.empty() && !(collision.generate && collision.has))); }
    void fit_shape(CollisionShapeSettings& r_shape) const;
    void add_shape(CollisionType p_type);
    void _frame(EditorContext& ctx);

    bool skeleton_dirty() const { return rig_preview.skinned && rig_preview.skeleton != rig_preview.rig.skeleton; }
    bool dirty() const override { return skeleton_dirty() || shapes_dirty(); }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
