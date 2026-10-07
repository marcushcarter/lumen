#include <editor/docking/center_view/debugger.h>
#include <editor/docking/tab_strip.h>
#include <editor/docking/center_view/debug_tabs/output_tab.h>
#include <editor/docking/center_view/debug_tabs/profiler_tab.h>
#include <editor/docking/center_view/debug_tabs/memory_tab.h>
#include <editor/docking/center_view/asset_manager/asset_manager.h>
#include <core/base/error.h>
#include <IconsFontAwesome6.h>
#include <cfloat>

namespace lumen {

void Debugger::initialize()
{
    tabs.push_back(std::make_unique<AssetManagerDebugTab>());
    tabs.push_back(std::make_unique<ProfilerDebugTab>());
    tabs.push_back(std::make_unique<MemoryDebugTab>());
    tabs.push_back(std::make_unique<OutputDebugTab>());
}

void Debugger::draw_content(EditorContext& ctx)
{
    if (active >= 0 && active < (int)tabs.size()) tabs[active]->draw(ctx);
}

void Debugger::draw_strip(ImVec2 p_min, ImVec2 p_max)
{
    const float h = p_max.y - p_min.y;
    float natural = 0.0f;
    for (const auto& t : tabs) natural += TabStrip::natural_width(t->name());

    TabStrip strip;
    strip.begin("##debug_strip", p_min, p_max, TabStrip::Edge::BOTTOM, natural, h);
    for (int i = 0; i < (int)tabs.size(); ++i) {
        if (!strip.tab(tabs[i]->name(), !collapsed && i == active)) continue;
        if (i == active) collapsed = !collapsed;
        else { active = i; collapsed = false; }
    }
    if (strip.trailing_button(collapsed ? ICON_FA_CHEVRON_UP : ICON_FA_CHEVRON_DOWN)) collapsed = !collapsed;
    strip.end();
}
    
}