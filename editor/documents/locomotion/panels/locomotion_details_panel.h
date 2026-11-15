#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct LocomotionDocument;

struct LocomotionDetailsPanel : Panel
{
    LocomotionDocument* tab = nullptr;

    const char* name() const override { return ICON_FA_PERSON_WALKING "  Locomotion"; }
    static constexpr float PAD_MARGIN = 0.1f;

    void _draw_pad();
    void draw_contents(EditorContext& ctx) override;
};

}
