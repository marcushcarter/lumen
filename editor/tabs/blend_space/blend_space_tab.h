#pragma once
#include <core/assets/lanim.h>
#include <core/assets/lblendspace.h>
#include <editor/assets/rig_preview.h>
#include <editor/tabs/asset_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <unordered_map>
#include <vector>

namespace lumen {

struct BlendSpaceTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::BLEND_SPACE;
    static constexpr const char* ICON = ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT;

    struct LoadedClip {
        LAnimation clip;
        std::vector<int32_t> map;
        uint64_t mapped_version = UINT64_MAX;
        bool failed = false;
    };

    LBlendSpace space;
    LBlendSpace saved;
    RigPreview rig_preview;
    float preview_value = 0.0f;
    float preview_direction = 0.0f;
    bool playing = true;
    float phase = 0.0f;

    std::unordered_map<Guid, LoadedClip> _clips;
    std::vector<float> _weights;
    std::vector<JointTransform> _pose;
    std::vector<std::vector<JointTransform>> _sample_poses;
    Guid _shown_mesh;
    bool _framed = false;

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_blend_space; }
    const LoadedClip* clip(EditorContext& ctx, Guid p_guid);
    Guid preview_mesh(EditorContext& ctx);
    void weights(EditorContext& ctx, float p_value, float p_direction, std::vector<float>& r_weights);
    void _sync_preview(EditorContext& ctx);
    void _pose_preview(EditorContext& ctx, float p_dt);

    const char* title() const override { return _title.empty() ? "Blend Space" : _title.c_str(); }
    bool dirty() const override { return space != saved; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
