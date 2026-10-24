#pragma once
#include <editor/drawer/drawer_tab.h>
#include <cstdint>
#include <vector>

namespace lumen {
    
struct MemoryDrawerTab : DrawerTab
{
    uint64_t frame_counter = 0;
    uint64_t peak_bytes = 0;

    std::vector<float> detailed_frag;
    bool detailed_valid = false;

    uint32_t max_rows = 100;
    
    const char* name() const override { return "Memory"; }
    void draw(EditorContext& ctx) override;

    void _draw_transients(EditorContext& ctx);
};

}