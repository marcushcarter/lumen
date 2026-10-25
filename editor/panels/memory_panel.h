#pragma once
#include <editor/layout/panel.h>
#include <cstdint>
#include <vector>

namespace lumen {
    
struct MemoryPanel : Panel
{
    uint64_t frame_counter = 0;
    uint64_t peak_bytes = 0;

    std::vector<float> detailed_frag;
    bool detailed_valid = false;

    uint32_t max_rows = 100;
    
    const char* name() const override { return "Memory"; }
    DockZone default_zone() const override { return DockZone::BOTTOM; }
    bool default_open() const override { return false; }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar; }
    ImVec2 window_padding() const override { return ui_style().dock.drawer_padding; }
    void draw_contents(EditorContext& ctx) override;

    void _draw_transients(EditorContext& ctx);
};

}