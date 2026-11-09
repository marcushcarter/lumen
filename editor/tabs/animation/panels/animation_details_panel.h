#pragma once
#include <editor/layout/panel.h>
#include <editor/tabs/animation/animation_tab.h>

namespace lumen {

struct AnimationDetailsPanel : Panel
{
    AnimationTab* tab = nullptr;

    const char* name() const override { return ICON_FA_PERSON_RUNNING "  Animation"; }
    DockZone default_zone() const override { return DockZone::RIGHT_TOP; }
    bool pinned() const override { return true; }

    void _draw_warnings(EditorContext& ctx);
    void draw_contents(EditorContext& ctx) override;
};

}
