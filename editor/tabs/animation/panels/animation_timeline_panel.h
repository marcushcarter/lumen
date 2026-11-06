#pragma once
#include <editor/layout/panel.h>
#include <editor/tabs/animation/animation_tab.h>

namespace lumen {

struct AnimationTimelinePanel : Panel
{
    AnimationTab* tab = nullptr;
    bool scrubbing = false;
    bool was_playing = false;

    const char* name() const override { return ICON_FA_FILM "  Timeline"; }
    DockZone default_zone() const override { return DockZone::BOTTOM; }
    bool pinned() const override { return true; }

    void _draw_strip(ImVec2 p_min, ImVec2 p_max);
    void draw_contents(EditorContext& ctx) override;
};

}
