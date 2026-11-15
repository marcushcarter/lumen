#include <editor/application/editor_settings.h>
#include <editor/ui/ui_style.h>
#include <core/base/color.h>
#include <cmath>

namespace lumen {

void Theme::apply() const
{
    UiStyle& ui = ui_style();
    ui.light = light;
    ui.pal = light ? UiStyle::palette_light() : UiStyle::palette_dark();
    const UiStyle::Palette& p = ui.pal;
    auto hex = [](uint32_t p_rgba) { return ui_hex4(p_rgba); };

    if (light) ImGui::StyleColorsLight();
    else ImGui::StyleColorsDark();
    ImVec4* c = ImGui::GetStyle().Colors;

    c[ImGuiCol_Text] = hex(p.TEXT);
    c[ImGuiCol_TextDisabled] = hex(p.TEXT_DIM);
    c[ImGuiCol_TextLink] = hex(p.LINK);
    c[ImGuiCol_TextSelectedBg] = hex(p.ACCENT_SELECT);
    c[ImGuiCol_UnsavedMarker] = hex(p.TEXT);

    c[ImGuiCol_WindowBg] = hex(p.BACK);
    c[ImGuiCol_ChildBg] = hex(p.BACK);
    c[ImGuiCol_PopupBg] = hex(p.NUM);
    c[ImGuiCol_MenuBarBg] = hex(p.TOPBAR);
    c[ImGuiCol_TitleBg] = hex(p.TOPBAR);
    c[ImGuiCol_TitleBgActive] = hex(p.TOPBAR);
    c[ImGuiCol_TitleBgCollapsed] = hex(p.TOPBAR);
    c[ImGuiCol_DockingEmptyBg] = hex(p.EDITOR_BORDER);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);

    c[ImGuiCol_FrameBg] = hex(p.NUM);
    c[ImGuiCol_FrameBgHovered] = hex(p.NUM_HOVER);
    c[ImGuiCol_FrameBgActive] = hex(p.NUM_ACTIVE);
    c[ImGuiCol_CheckboxSelectedBg] = hex(p.ACCENT);
    c[ImGuiCol_InputTextCursor] = hex(p.TEXT_CURSOR);

    c[ImGuiCol_Border] = hex(p.OUTLINE);
    c[ImGuiCol_BorderShadow] = hex(p.EMBOSS);
    c[ImGuiCol_Separator] = hex(p.SEPARATOR);
    c[ImGuiCol_SeparatorHovered] = hex(p.ACCENT_HOVER);
    c[ImGuiCol_SeparatorActive] = hex(p.ACCENT);
    c[ImGuiCol_TreeLines] = hex(p.OUTLINE);

    c[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = hex(p.ACCENT_HOVER);
    c[ImGuiCol_ResizeGripActive] = hex(p.ACCENT);

    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = hex(p.SCROLL);
    c[ImGuiCol_ScrollbarGrabHovered] = hex(p.SCROLL_HOVER);
    c[ImGuiCol_ScrollbarGrabActive] = hex(p.SCROLL_ACTIVE);

    c[ImGuiCol_Header] = hex(p.LIST_SELECTED);
    c[ImGuiCol_HeaderHovered] = hex(p.LIST_HOVER);
    c[ImGuiCol_HeaderActive] = hex(p.LIST_ACTIVE);
    c[ImGuiCol_Button] = hex(p.NUM);
    c[ImGuiCol_ButtonHovered] = hex(p.NUM_HOVER);
    c[ImGuiCol_ButtonActive] = hex(p.ACCENT);
    c[ImGuiCol_CheckMark] = hex(p.CHECK);
    c[ImGuiCol_SliderGrab] = hex(p.ACCENT);
    c[ImGuiCol_SliderGrabActive] = hex(p.ACCENT_HOVER);

    c[ImGuiCol_Tab] = hex(p.TAB);
    c[ImGuiCol_TabHovered] = hex(p.TAB_HOVER);
    c[ImGuiCol_TabSelected] = hex(p.TAB_ACTIVE);
    c[ImGuiCol_TabSelectedOverline] = hex(p.ACCENT);
    c[ImGuiCol_TabDimmed] = hex(p.TAB);
    c[ImGuiCol_TabDimmedSelected] = hex(p.TAB_ACTIVE);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    c[ImGuiCol_DockingPreview] = hex(p.ACCENT_SELECT);
    c[ImGuiCol_DragDropTarget] = hex(p.ACCENT);
    c[ImGuiCol_DragDropTargetBg] = hex(p.ACCENT_DROP);
    c[ImGuiCol_NavCursor] = hex(p.ACCENT);
    c[ImGuiCol_NavWindowingHighlight] = hex(p.ACCENT_HOVER);

    c[ImGuiCol_PlotLines] = hex(p.TEXT_DIM);
    c[ImGuiCol_PlotLinesHovered] = hex(p.ACCENT);
    c[ImGuiCol_PlotHistogram] = hex(p.ACCENT);
    c[ImGuiCol_PlotHistogramHovered] = hex(p.ACCENT_HOVER);

    c[ImGuiCol_TableHeaderBg] = hex(p.PANEL);
    c[ImGuiCol_TableBorderStrong] = hex(p.EDITOR_BORDER);
    c[ImGuiCol_TableBorderLight] = hex(p.OUTLINE);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = hex(p.ROW_ALTERNATE);

    ui.build();
    ui.apply_to_imgui();
}

uint32_t Theme::to_colorref(const ImVec4& p_color)
{
    auto channel = [](float p_v) -> uint32_t {
        const float q = std::round((p_v < 0.0f ? 0.0f : (p_v > 1.0f ? 1.0f : p_v)) * 255.0f) / 255.0f;
        return (uint32_t)std::lround(color_linear_to_srgb(q) * 255.0f);
    };
    return channel(p_color.x) | (channel(p_color.y) << 8) | (channel(p_color.z) << 16);
}

}
