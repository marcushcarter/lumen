#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct ModelDocument;

struct ModelDetailsPanel : Panel
{
    ModelDocument* tab = nullptr;

    const char* name() const override { return ICON_FA_CUBES "  Model"; }

    void _draw_materials(EditorContext& ctx);
    void _draw_warnings(EditorContext& ctx);
    void draw_contents(EditorContext& ctx) override;
};

}
