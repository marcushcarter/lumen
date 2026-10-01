#pragma once
#include <editor/docking/center_view/debug_tab.h>
#include <core/rendering/render_graph_profiler.h>
#include <cstdint>
#include <vector>

namespace lumen {
    
struct MemoryDebugTab : DebugTab
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