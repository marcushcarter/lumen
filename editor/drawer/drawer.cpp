#include <editor/drawer/drawer.h>
#include <drivers/imgui/tab_strip.h>
#include <drivers/imgui/ui_style.h>
#include <imgui_internal.h>
#include <editor/drawer/content_browser_tab.h>
#include <editor/drawer/profiler_tab.h>
#include <editor/drawer/memory_tab.h>
#include <editor/drawer/output_tab.h>
#include <core/base/error.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cfloat>
#include <cstring>

namespace lumen {

bool DrawerState::is_open(const DrawerTab* p_tab) const
{
    return p_tab && std::find(open_tabs.begin(), open_tabs.end(), p_tab) != open_tabs.end();
}

void DrawerState::open(DrawerTab* p_tab)
{
    if (!p_tab) return;
    if (!is_open(p_tab)) open_tabs.push_back(p_tab);
    active = p_tab;
}

void DrawerState::close(DrawerTab* p_tab)
{
    auto it = std::find(open_tabs.begin(), open_tabs.end(), p_tab);
    if (it == open_tabs.end()) return;
    const size_t i = (size_t)(it - open_tabs.begin());
    open_tabs.erase(it);
    if (active != p_tab) return;
    if (open_tabs.empty()) active = nullptr;
    else active = open_tabs[i < open_tabs.size() ? i : open_tabs.size() - 1];
}

void DrawerState::toggle(DrawerTab* p_tab)
{
    if (active == p_tab) close(p_tab);
    else open(p_tab);
}

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

DrawerTab* Drawer::find(const char* p_name) const
{
    for (const auto& t : tabs) if (std::strcmp(t->name(), p_name) == 0) return t.get();
    return nullptr;
}

void Drawer::draw(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max, DrawerState& r_state)
{
    if (r_state.open_tabs.empty()) return;
    if (!r_state.is_open(r_state.active)) r_state.active = r_state.open_tabs.front();

    const UiStyle& ui = ui_style();
    const float tab_h = TabStrip::dock_height();
    const ImVec2 size(p_max.x - p_min.x, p_max.y - p_min.y);

    float natural = 0.0f;
    for (DrawerTab* t : r_state.open_tabs) natural += TabStrip::dock_natural_width(t->name());

    DrawerTab* closed = nullptr;
    TabStrip strip;
    strip.begin("##drawer_tabs", p_min, ImVec2(p_max.x, p_min.y + tab_h), TabStrip::Edge::TOP, natural, ui.dock_tab.inset_x);
    strip.cursor_x += ui.dock_tab.inset_x;
    for (DrawerTab* t : r_state.open_tabs) {
        bool close = false;
        if (strip.dock_tab(t->name(), t == r_state.active, &close)) r_state.active = t;
        if (close) closed = t;
    }
    strip.end();

    if (closed) r_state.close(closed);
    if (!r_state.active) return;

    ImGui::SetCursorScreenPos(ImVec2(p_min.x, p_min.y + tab_h));
    ImGui::PushID(r_state.active->name());
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.panel);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ui.dock.drawer_padding);
    ImGui::BeginChild("##drawer", ImVec2(size.x, ImMax(1.0f, size.y - tab_h)), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    r_state.active->draw(ctx);
    ImGui::EndChild();
    ImGui::PopID();
}

void Drawer::draw_bar(ImVec2 p_min, ImVec2 p_max, DrawerState& r_state)
{
    const UiStyle& ui = ui_style();
    const char* console_label = ICON_FA_TERMINAL "  Cmd";
    float natural = 0.0f;
    for (const auto& t : tabs) natural += TabStrip::natural_width(t->name());

    const float console_chrome = ImGui::CalcTextSize(console_label).x + ui.console_bar.pad * 3.0f;
    const float h = p_max.y - p_min.y;
    const float free_w = (p_max.x - p_min.x) - natural - h - console_chrome;
    const float field_w = ImClamp(free_w, ui.console_bar.field_min_width, ui.console_bar.field_width);

    TabStrip strip;
    strip.begin("##drawer_bar", p_min, p_max, TabStrip::Edge::BOTTOM, natural, h + console_chrome + field_w);
    for (const auto& up : tabs) {
        DrawerTab* t = up.get();
        if (strip.button(t->name(), r_state.is_open(t))) r_state.toggle(t);
    }
    if (strip.console_field("##console_cmd", console_label, "Enter Console Command", console_buffer, sizeof(console_buffer), field_w)) {
        console_submit(console_buffer);
        console_buffer[0] = '\0';
    }
    if (!r_state.open_tabs.empty() && strip.trailing_button(ICON_FA_XMARK)) {
        r_state.open_tabs.clear();
        r_state.active = nullptr;
    }
    strip.end();
}

}