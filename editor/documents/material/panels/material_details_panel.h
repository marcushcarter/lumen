#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct MaterialDocument;

struct MaterialDetailsPanel : Panel
{
    MaterialDocument* tab = nullptr;

    const char* name() const override { return ICON_FA_PALETTE "  Material"; }
    
    void _draw_warnings(EditorContext& ctx);
    void draw_contents(EditorContext& ctx) override;
};

}
