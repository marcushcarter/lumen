#pragma once
#include <editor/layout/panel.h>
#include <editor/documents/animation/animation_document.h>

namespace lumen {

struct AnimationTimelinePanel : Panel
{
    AnimationDocument* tab = nullptr;
    bool scrubbing = false;
    bool was_playing = false;

    const char* name() const override { return ICON_FA_FILM "  Timeline"; }
    PanelSlot slot() const override { return PanelSlot::BOTTOM; }

    void _draw_strip(ImVec2 p_min, ImVec2 p_max);
    void draw_contents(EditorContext& ctx) override;
};

}
