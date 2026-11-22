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
    static constexpr float BONE_PICK_PX = 12.0f;

    LMeshStats mesh_stats;
    RigPreview rig_preview;

    struct CollisionView {
        bool has = false;
        CollisionSettings settings;
        CollisionSettings edit;
        LCollision data;
        bool loaded = false;
        std::vector<uint32_t> edges;
        std::filesystem::file_time_type time{};
    };
    CollisionView collision;
    int32_t selected_shape = -1;

    int32_t selected_bone = -1;

    std::vector<Guid> materials;
    std::vector<Guid> saved_materials;
    std::vector<Guid> _preview_materials;

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
    void _refresh_materials(EditorContext& ctx);

    bool shapes_mode() const { return collision.edit.type == CollisionType::SHAPES; }
    bool shapes_dirty() const { return shapes_mode() && (!(collision.edit == collision.settings) || collision.has == collision.edit.shapes.empty()); }
    void fit_shape(CollisionShapeSettings& r_shape) const;
    void add_shape(CollisionType p_type);
    bool source_used(CollisionType p_type, int32_t p_slot, int32_t p_except = -1) const;
    int32_t free_source(CollisionType p_type) const;
    void _frame(EditorContext& ctx);

    void _pick_bone(EditorContext& ctx);
    void _draw_bones(DebugDraw& r_draw) const;
    bool materials_dirty() const { return materials != saved_materials; }
    bool dirty() const override { return shapes_dirty() || materials_dirty(); }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
