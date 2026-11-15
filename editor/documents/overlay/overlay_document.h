#pragma once
#include <core/assets/loverlay.h>
#include <editor/assets/anim_preview.h>
#include <editor/assets/rig_preview.h>
#include <editor/documents/asset_document.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <vector>

namespace lumen {

struct OverlayDocument : AssetDocument
{
    static constexpr DocumentKind KIND = DocumentKind::OVERLAY;
    static constexpr const char* ICON = ICON_FA_LAYER_GROUP;

    LOverlay overlay;
    LOverlay saved;
    RigPreview rig_preview;
    AnimPreview anim;
    float preview_value = 0.0f;
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

    const char* _type_name() const override { return "Overlay"; }
    const LBlendSpace* blend_space(EditorContext& ctx);
    Guid preview_mesh(EditorContext& ctx);
    void _sync_preview(EditorContext& ctx);
    void _pose_preview(EditorContext& ctx, float p_dt);

    bool dirty() const override { return overlay != saved; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
