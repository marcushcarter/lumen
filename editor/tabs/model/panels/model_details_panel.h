#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct ModelTab;

struct ModelDetailsPanel : Panel
{
    ModelTab* tab = nullptr;

    const char* name() const override { return ICON_FA_CUBES "  Model"; }
    DockZone default_zone() const override { return DockZone::RIGHT_TOP; }
    // bool closable() const override { return false; }
    bool pinned() const override { return true; }
    void draw_contents(EditorContext& ctx) override;
};

}