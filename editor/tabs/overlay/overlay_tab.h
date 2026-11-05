#pragma once
#include <core/assets/loverlay.h>
#include <editor/assets/anim_preview.h>
#include <editor/assets/rig_preview.h>
#include <editor/tabs/asset_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <vector>

namespace lumen {

struct OverlayTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::OVERLAY;
    static constexpr const char* ICON = ICON_FA_LAYER_GROUP;

    LOverlay overlay;
    LOverlay saved;
    RigPreview rig_preview;
    AnimPreview anim;
    float preview_value = 0.0f;
    float preview_direction = 0.0f;
    float preview_weight = 1.0f;
    bool playing = true;
    float phase = 0.0f;
    float time = 0.0f;
    std::vector<float> mask;

    std::vector<JointTransform> _pose;
    Guid _shown_mesh;
    Guid _found_skeleton;
    Guid _found_mesh;
    bool _framed = false;

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_overlay; }
    const LBlendSpace* blend_space(EditorContext& ctx);
    Guid preview_mesh(EditorContext& ctx);
    void _sync_preview(EditorContext& ctx);
    void _pose_preview(EditorContext& ctx, float p_dt);

    const char* title() const override { return _title.empty() ? "Overlay" : _title.c_str(); }
    bool dirty() const override { return overlay != saved; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
