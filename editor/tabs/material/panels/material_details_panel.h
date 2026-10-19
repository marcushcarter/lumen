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
    bool closable() const override { return false; }
    void draw_contents(EditorContext& ctx) override;
};

}