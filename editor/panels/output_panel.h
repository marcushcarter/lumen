#pragma once
#include <editor/layout/panel.h>

namespace lumen {

void console_submit(const char* p_command);

struct OutputPanel : Panel
{
    const char* name() const override { return "Output Log"; }
    DockZone default_zone() const override { return DockZone::BOTTOM; }
    bool default_open() const override { return false; }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar; }
    ImVec2 window_padding() const override { return ui_style().dock.drawer_padding; }
    void draw_contents(EditorContext& ctx) override;
};

}
