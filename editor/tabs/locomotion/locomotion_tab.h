#pragma once
#include <core/assets/llocomotion.h>
#include <core/game/character.h>
#include <editor/assets/anim_preview.h>
#include <editor/assets/rig_preview.h>
#include <editor/tabs/asset_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>
#include <vector>

namespace lumen {

struct LocomotionTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::LOCOMOTION;
    static constexpr const char* ICON = ICON_FA_PERSON_WALKING;

    LLocomotion character;
    LLocomotion saved;
    RigPreview rig_preview;
    AnimPreview anim;
    Character state;
    float preview_speed = 0.0f;
    float preview_direction = 0.0f;
    float preview_look = 0.0f;
    bool preview_aim = false;
    float preview_fall_speed = 4.0f;
    bool playing = true;

    std::vector<JointTransform> _pose;
    Guid _shown_mesh;
    bool _framed = false;

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_locomotion; }
    const char* title() const override { return _title.empty() ? "Locomotion" : _title.c_str(); }
    bool dirty() const override { return character != saved; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;

    Guid preview_mesh(EditorContext& ctx);
    void _sync_preview(EditorContext& ctx);
    void _pose_preview(EditorContext& ctx, float p_dt);
};

}
