#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct MeshDocument;

struct MeshDetailsPanel : Panel
{
    MeshDocument* tab = nullptr;

    const char* name() const override { return ICON_FA_CUBE "  Mesh"; }

    void _draw_warnings(EditorContext& ctx);
    void draw_contents(EditorContext& ctx) override;
};

}
