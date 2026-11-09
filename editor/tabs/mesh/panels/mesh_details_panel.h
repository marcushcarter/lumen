#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct MeshTab;

struct MeshDetailsPanel : Panel
{
    MeshTab* tab = nullptr;

    const char* name() const override { return ICON_FA_CUBE "  Mesh"; }
    DockZone default_zone() const override { return DockZone::RIGHT_TOP; }
    bool pinned() const override { return true; }

    void _draw_warnings(EditorContext& ctx);
    void draw_contents(EditorContext& ctx) override;
};

}
