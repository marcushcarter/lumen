#pragma once
#include <editor/layout/panel.h>
#include <editor/documents/animation/animation_document.h>

namespace lumen {

struct AnimationDetailsPanel : Panel
{
    AnimationDocument* tab = nullptr;

    const char* name() const override { return ICON_FA_PERSON_RUNNING "  Animation"; }

    void _draw_warnings(EditorContext& ctx);
    void draw_contents(EditorContext& ctx) override;
};

}
