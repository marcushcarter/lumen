#pragma once
#include <editor/docking/center_view/debug_tab.h>

namespace lumen {
    
void console_submit(const char* p_command);

struct OutputDebugTab : DebugTab
{
    const char* name() const override { return "Ouput Log"; }
    void draw(EditorContext& ctx) override;
};

}