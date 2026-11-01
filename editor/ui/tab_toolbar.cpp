#include <editor/ui/tab_toolbar.h>
#include <editor/application/editor_context.h>
#include <editor/tabs/editor_tab.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>

namespace lumen {

void TabToolbar::begin()
{
    const UiStyle& ui = ui_style();
    min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    max = ImVec2(min.x + size.x, min.y + size.y);
    x = min.x;

    ImGui::PushClipRect(min, max, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(min, max, ui.color.chrome);
    dl->AddRectFilled(ImVec2(min.x, max.y - ui.stroke.hairline), max, ui.color.line);
}

void TabToolbar::end()
{
    ImGui::PopClipRect();
}

ImVec2 TabToolbar::inner_max() const
{
    return ImVec2(max.x, max.y - ui_style().stroke.hairline);
}

bool TabToolbar::button(const char* p_label, const char* p_tooltip, bool p_active, bool p_enabled)
{
    const UiStyle& ui = ui_style();
    const char* label_end = ImGui::FindRenderedTextEnd(p_label);
    const ImVec2 size(ImGui::CalcTextSize(p_label, label_end).x + ui.tab_strip.pad_x * 2.0f, max.y - min.y);
    const ImVec2 p(x, min.y);
    x += size.x;

    ImGui::SetCursorScreenPos(p);
    const bool pressed = ImGui::InvisibleButton(p_label, size);
    const bool hovered = p_enabled && ImGui::IsItemHovered();
    const bool held = p_enabled && ImGui::IsItemActive();
    if (p_tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", p_tooltip);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 bmax(p.x + size.x, p.y + size.y - ui.stroke.hairline);
    if (p_active || held) dl->AddRectFilled(p, bmax, ui.color.panel);
    else if (hovered) dl->AddRectFilled(p, bmax, ui.color.hover);
    if (p_active) dl->AddRectFilled(ImVec2(p.x, bmax.y - ui.tab_strip.accent_height), bmax, ui.color.accent);

    ImU32 col = (p_active || hovered) ? ui.color.text : ui.color.text_dim;
    if (!p_enabled) col = ui_with_alpha(ui.color.text_dim, ui.alpha.disabled);
    ui_text_centered(dl, p, ImVec2(p.x + size.x, p.y + size.y), col, p_label, label_end);
    return pressed && p_enabled;
}

bool TabToolbar::save(EditorContext& ctx, EditorTab& r_tab)
{
    if (!button(ICON_FA_FLOPPY_DISK "  Save", "Save (Ctrl+S)")) return false;
    r_tab.save(ctx);
    return true;
}

void TabToolbar::gap()
{
    x += ui_style().toolbar.padding_x;
}

void TabToolbar::item(float p_width)
{
    const float h = max.y - min.y - ui_style().stroke.hairline;
    ImGui::SetCursorScreenPos(ImVec2(x, min.y + ImFloor((h - ImGui::GetFrameHeight()) * 0.5f)));
    ImGui::SetNextItemWidth(p_width);
    x += p_width;
}

void TabToolbar::text(const char* p_text)
{
    const UiStyle& ui = ui_style();
    const ImVec2 size = ImGui::CalcTextSize(p_text);
    const float h = max.y - min.y - ui.stroke.hairline;
    ImGui::GetWindowDrawList()->AddText(ImVec2(x, min.y + ImFloor((h - size.y) * 0.5f)), ui.color.text_dim, p_text);
    x += size.x;
}

}
