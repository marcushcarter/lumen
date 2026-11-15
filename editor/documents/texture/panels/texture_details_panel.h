#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct TextureDocument;

struct TextureDetailsPanel : Panel
{
    TextureDocument* tab = nullptr;

    const char* name() const override { return ICON_FA_IMAGE "  Texture"; }

    void _draw_warnings(EditorContext& ctx);
    void draw_contents(EditorContext& ctx) override;
};

}
