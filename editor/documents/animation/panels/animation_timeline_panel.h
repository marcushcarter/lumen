#pragma once
#include <editor/layout/panel.h>
#include <editor/documents/animation/animation_document.h>

namespace lumen {

struct AnimationTimelinePanel : Panel
{
    AnimationDocument* tab = nullptr;
    bool scrubbing = false;
    bool was_playing = false;
    float _track_min = 0.0f;
    float _track_max = 0.0f;

    const char* name() const override { return ICON_FA_FILM "  Timeline"; }
    PanelSlot slot() const override { return PanelSlot::BOTTOM; }

    void _draw_strip(ImVec2 p_min, ImVec2 p_max);
    void _draw_events(float p_y, float p_h);
    void _draw_contacts(float p_x, float p_y, float p_h);
    void _draw_graph(float p_x, ImVec2 p_min, ImVec2 p_max);
    void _draw_playhead(float p_y0, float p_y1);
    float _time_x(float p_time) const;
    void draw_contents(EditorContext& ctx) override;
};

}
