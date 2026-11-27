#pragma once
#include <core/animation/clip_player.h>
#include <core/assets/lanim.h>
#include <editor/assets/rig_preview.h>
#include <editor/documents/asset_document.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <vector>

namespace lumen {

struct DebugDraw;

enum class ClipGraph : int32_t
{
    PHASE,
    DISTANCE,
    REMAINING,
    SPEED,
    CURVE,
};

struct AnimationDocument : AssetDocument
{
    static constexpr DocumentKind KIND = DocumentKind::ANIMATION;
    static constexpr const char* ICON = ICON_FA_PERSON_RUNNING;
    static constexpr double CLIP_POLL_S = 1.0;

    LAnimation clip;
    ClipPlayer player;
    RigPreview rig_preview;
    Guid preview_mesh;
    Guid saved_preview_mesh;
    bool loop = true;
    bool saved_loop = true;
    uint32_t missing_tracks = 0;
    std::vector<ClipEvent> events;
    std::vector<ClipEvent> saved_events;
    std::vector<ClipCurve> curves;
    std::vector<ClipCurve> saved_curves;
    ClipGraph graph = ClipGraph::PHASE;
    int32_t graph_curve = -1;
    int32_t open_event = -1;
    int32_t open_curve = -1;

    std::vector<int32_t> _track_map;
    ClipRetarget _retarget;
    std::vector<JointTransform> _local;
    uint64_t _mapped_version = UINT64_MAX;
    Guid _shown_mesh;
    bool _framed = false;
    std::filesystem::file_time_type _clip_time{};
    double _clip_poll = -1.0e9;
    int32_t _foot_joint[(int)ClipFootSide::COUNT] = { SKELETON_NO_JOINT, SKELETON_NO_JOINT };

    const char* _type_name() const override { return "Animation"; }
    void _poll_clip(EditorContext& ctx, bool p_force);
    void _sync_preview(EditorContext& ctx);
    void _pose();
    void _draw_meta(DebugDraw& r_draw) const;
    float preview_scale() const { return _retarget.active() ? _retarget.scale : 1.0f; }
    float clip_speed_at(float p_time) const;

    bool dirty() const override { return loop != saved_loop || preview_mesh != saved_preview_mesh || events != saved_events || curves != saved_curves; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
