#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct AnimGraphTab;

struct AnimGraphCanvasPanel : Panel
{
    AnimGraphTab* tab = nullptr;

    const char* name() const override { return ICON_FA_DIAGRAM_PROJECT "  Graph"; }
    DockZone default_zone() const override { return DockZone::RIGHT_TOP; }
    bool pinned() const override { return true; }
    void draw_contents(EditorContext& ctx) override;
};

}
