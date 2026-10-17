#pragma once
#include <editor/editor_context.h>
#include <imgui.h>

namespace lumen {

struct DrawerTab
{
    virtual ~DrawerTab() = default;
    virtual const char* name() const = 0;
    virtual void draw(EditorContext& ctx) = 0;
};
    
}