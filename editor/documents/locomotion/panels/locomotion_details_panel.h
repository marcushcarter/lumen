#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct LocomotionDocument;

struct LocomotionDetailsPanel : Panel
{
    LocomotionDocument* tab = nullptr;
    uint32_t last_filled = UINT32_MAX;

    const char* name() const override { return ICON_FA_PERSON_WALKING "  Locomotion Set"; }

    void draw_contents(EditorContext& ctx) override;
};

}
