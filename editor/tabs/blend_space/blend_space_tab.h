#pragma once
#include <core/assets/lblendspace.h>
#include <editor/assets/anim_preview.h>
#include <editor/assets/rig_preview.h>
#include <editor/tabs/asset_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <vector>

namespace lumen {

struct BlendSpaceTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::BLEND_SPACE;
    static constexpr const char* ICON = ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT;

    using LoadedClip = AnimPreview::LoadedClip;

    LBlendSpace space;
    LBlendSpace saved;
    RigPreview rig_preview;
    AnimPreview anim;
    std::vector<Guid> preview_overlays;
    float preview_value = 0.0f;
    bool playing = true;
    float phase = 0.0f;
    std::vector<float> weights;
    float overlay_time = 0.0f;

    std::vector<JointTransform> _pose;
    Guid _shown_mesh;
    bool _framed = false;

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_blend_space; }
    const LoadedClip* clip(EditorContext& ctx, Guid p_guid);
    Guid preview_mesh(EditorContext& ctx);
    void _sync_preview(EditorContext& ctx);
    void _pose_preview(EditorContext& ctx, float p_dt);

    const char* title() const override { return _title.empty() ? "Blend Space" : _title.c_str(); }
    bool dirty() const override { return space != saved; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
