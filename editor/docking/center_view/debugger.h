#pragma once
#include <editor/docking/center_view/debug_tab.h>
#include <imgui.h>
#include <vector>
#include <memory>

namespace lumen {
    
struct Debugger
{
    std::vector<std::unique_ptr<DebugTab>> tabs;
    int active = 0;
    bool collapsed = false;

    void initialize();
    void draw_content(EditorContext& ctx);
    void draw_strip(ImVec2 p_min, ImVec2 p_max);
};

}