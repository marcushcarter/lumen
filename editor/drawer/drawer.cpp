#include <editor/drawer/drawer.h>
#include <editor/ui/tab_strip.h>
#include <drivers/imgui/ui_style.h>
#include <imgui_internal.h>
#include <editor/drawer/content_browser_tab.h>
#include <editor/drawer/profiler_tab.h>
#include <editor/drawer/memory_tab.h>
#include <editor/drawer/output_tab.h>
#include <core/base/error.h>
#include <IconsFontAwesome6.h>
#include <cfloat>

namespace lumen {

void Drawer::initialize()
{
    tabs.push_back(std::make_unique<ContentBrowserDrawerTab>());
    tabs.push_back(std::make_unique<ProfilerDrawerTab>());
    tabs.push_back(std::make_unique<MemoryDrawerTab>());
    tabs.push_back(std::make_unique<OutputDrawerTab>());
}

void Drawer::shutdown()
{
    tabs.clear();
}

bool Drawer::_visible(const DrawerState& p_state, int p_index) const
{
    return p_index >= 0 && p_index < (int)tabs.size() && (p_state.mask >> (uint32_t)p_index) & 1u;
}

void Drawer::_resolve_active(DrawerState& r_state) const
{
    if (_visible(r_state, r_state.active)) return;
    for (int i = 0; i < (int)tabs.size(); ++i) {
        if (!_visible(r_state, i)) continue;
        r_state.active = i;
        return;
    }
}

void Drawer::draw_content(EditorContext& ctx, DrawerState& r_state)
{
    _resolve_active(r_state);
    if (_visible(r_state, r_state.active)) tabs[r_state.active]->draw(ctx);
}

void Drawer::draw_strip(ImVec2 p_min, ImVec2 p_max, DrawerState& r_state)
{
    _resolve_active(r_state);
    const UiStyle& ui = ui_style();
    const char* console_label = ICON_FA_TERMINAL "  Cmd";
    const float h = p_max.y - p_min.y;
    float natural = 0.0f;
    for (int i = 0; i < (int)tabs.size(); ++i) if (_visible(r_state, i)) natural += TabStrip::natural_width(tabs[i]->name());

    const float console_chrome = ImGui::CalcTextSize(console_label).x + ui.console_bar.pad * 3.0f;
    const float free_w = (p_max.x - p_min.x) - natural - h - console_chrome;
    const float field_w = ImClamp(free_w, ui.console_bar.field_min_width, ui.console_bar.field_width);

    TabStrip strip;
    strip.begin("##drawer_strip", p_min, p_max, TabStrip::Edge::BOTTOM, natural, h + console_chrome + field_w);
    for (int i = 0; i < (int)tabs.size(); ++i) {
        if (!_visible(r_state, i)) continue;
        if (!strip.tab(tabs[i]->name(), !r_state.collapsed && i == r_state.active)) continue;
        if (i == r_state.active) r_state.collapsed = !r_state.collapsed;
        else { r_state.active = i; r_state.collapsed = false; }
    }
    if (strip.console_field("##console_cmd", console_label, "Enter Console Command", console_buffer, sizeof(console_buffer), field_w)) {
        console_submit(console_buffer);
        console_buffer[0] = '\0';
    }
    if (strip.trailing_button(r_state.collapsed ? ICON_FA_CHEVRON_UP : ICON_FA_CHEVRON_DOWN)) r_state.collapsed = !r_state.collapsed;
    strip.end();
}

}