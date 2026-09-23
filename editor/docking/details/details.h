#pragma once
#include <editor/docking/panel.h>

namespace lumen {

struct DetailsPanel : Panel
{
    const char* name() const override { return "Details"; }
    DockZone default_zone() const override { return DockZone::RightBottom; }
    void draw_contents(EditorContext&) override;
};
    
}