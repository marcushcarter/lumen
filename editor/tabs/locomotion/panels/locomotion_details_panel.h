#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct LocomotionTab;

struct LocomotionDetailsPanel : Panel
{
    LocomotionTab* tab = nullptr;

    const char* name() const override { return ICON_FA_PERSON_WALKING "  Locomotion"; }
    DockZone default_zone() const override { return DockZone::RIGHT_TOP; }
    bool pinned() const override { return true; }
    static constexpr float PAD_MARGIN = 0.1f;

    void _draw_pad();
    void draw_contents(EditorContext& ctx) override;
};

}
