#pragma once
#include <editor/layout/panel.h>
#include <editor/tabs/animation/animation_tab.h>

namespace lumen {

struct AnimationTimelinePanel : Panel
{
    static constexpr float SPEED_WIDTH = 80.0f;

    AnimationTab* tab = nullptr;

    const char* name() const override { return ICON_FA_FILM "  Timeline"; }
    DockZone default_zone() const override { return DockZone::BOTTOM; }
    bool pinned() const override { return true; }
    void draw_contents(EditorContext& ctx) override;
};

}
