#pragma once
#include <core/animation/clip_player.h>
#include <core/assets/lanim.h>
#include <editor/assets/rig_preview.h>
#include <editor/tabs/asset_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <vector>

namespace lumen {

struct AnimationTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::ANIMATION;
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

    std::vector<int32_t> _track_map;
    std::vector<JointTransform> _local;
    uint64_t _mapped_version = UINT64_MAX;
    Guid _shown_mesh;
    bool _framed = false;
    std::filesystem::file_time_type _clip_time{};
    double _clip_poll = -1.0e9;

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_animation; }
    void _poll_clip(EditorContext& ctx, bool p_force);
    void _sync_preview(EditorContext& ctx);
    void _pose();

    const char* title() const override { return _title.empty() ? "Animation" : _title.c_str(); }
    bool dirty() const override { return loop != saved_loop || preview_mesh != saved_preview_mesh; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
