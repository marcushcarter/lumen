#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct MaterialTab;

struct MaterialDetailsPanel : Panel
{
    MaterialTab* tab = nullptr;

    const char* name() const override { return ICON_FA_PALETTE "  Material"; }
    DockZone default_zone() const override { return DockZone::RIGHT_TOP; }
    bool pinned() const override { return true; }
    void draw_contents(EditorContext& ctx) override;
};

}
