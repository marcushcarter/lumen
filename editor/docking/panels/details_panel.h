#pragma once
#include <editor/docking/panel.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct DetailsPanel : Panel
{
    const char* name() const override { return ICON_FA_IMAGE "  Details"; }
    DockZone default_zone() const override { return DockZone::RIGHT_BOTTOM; }
    void draw_contents(EditorContext& ctx) override;

    void _component(const std::string_view title, )
};

}