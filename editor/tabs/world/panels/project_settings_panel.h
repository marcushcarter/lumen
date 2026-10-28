#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>
#include <cstdint>

namespace lumen {

struct ProjectSettingsPanel : Panel
{
    static constexpr uint32_t MAX_TITLE = 256;

    char title_buf[MAX_TITLE] = {};

    const char* name() const override { return ICON_FA_SLIDERS "  Project Settings"; }
    DockZone default_zone() const override { return DockZone::RIGHT_BOTTOM; }
    bool default_open() const override { return false; }
    void draw_contents(EditorContext& ctx) override;
};

}
