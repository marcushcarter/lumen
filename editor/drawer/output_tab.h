#pragma once
#include <editor/drawer/drawer_tab.h>

namespace lumen {
    
void console_submit(const char* p_command);

struct OutputDrawerTab : DrawerTab
{
    const char* name() const override { return "Ouput Log"; }
    void draw(EditorContext& ctx) override;
};

}