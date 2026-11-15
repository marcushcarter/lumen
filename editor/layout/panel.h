#pragma once
#include <editor/application/editor_context.h>
#include <editor/ui/ui_style.h>
#include <imgui.h>

namespace lumen {

enum class PanelSlot { SIDE, BOTTOM };

struct Panel
{
    virtual ~Panel() = default;
    virtual const char* name() const = 0;
    virtual const char* icon() const { return nullptr; }
    virtual bool closable() const { return false; }
    virtual PanelSlot slot() const { return PanelSlot::SIDE; }
    virtual void draw_contents(EditorContext& ctx) = 0;
    virtual ImGuiWindowFlags window_flags() const { return 0; }
    virtual ImVec2 window_padding() const { return ui_style().panel.padding; }
    virtual void shutdown() {}
};

struct PanelTab
{
    const char* name = nullptr;
    const char* icon = nullptr;
    bool active = false;
    bool closable = false;
    bool dirty = false;
};

struct PanelTabResult
{
    int clicked = -1;
    int closed = -1;
    bool menu = false;
};

float panel_header_height();
PanelTabResult panel_tab_bar(const PanelTab* p_tabs, int p_count, ImVec2 p_min, ImVec2 p_max, bool p_menu);
void panel_header(const char* p_title, const char* p_icon, ImVec2 p_min, ImVec2 p_max);
void panel_body(EditorContext& ctx, Panel& r_panel, ImVec2 p_min, ImVec2 p_max);
void panel_draw(EditorContext& ctx, Panel& r_panel, ImVec2 p_min, ImVec2 p_max);

}
