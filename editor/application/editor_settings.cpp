#include <editor/application/editor_settings.h>
#include <editor/ui/ui_style.h>
#include <core/base/color.h>
#include <cmath>

namespace lumen {

void Theme::apply() const
{
    using P = UiStyle::Palette;
    auto hex = [](uint32_t p_rgba) { return ui_hex4(p_rgba); };

    ImGui::StyleColorsDark();
    ImVec4* c = ImGui::GetStyle().Colors;

    c[ImGuiCol_Text] = hex(P::TEXT);
    c[ImGuiCol_TextDisabled] = hex(P::TEXT_DIM);
    c[ImGuiCol_TextLink] = hex(P::LINK);
    c[ImGuiCol_TextSelectedBg] = hex(P::ACCENT_SELECT);
    c[ImGuiCol_UnsavedMarker] = hex(P::TEXT);

    c[ImGuiCol_WindowBg] = hex(P::BACK);
    c[ImGuiCol_ChildBg] = hex(P::BACK);
    c[ImGuiCol_PopupBg] = hex(P::MENU_BACK);
    c[ImGuiCol_MenuBarBg] = hex(P::TOPBAR);
    c[ImGuiCol_TitleBg] = hex(P::TOPBAR);
    c[ImGuiCol_TitleBgActive] = hex(P::TOPBAR);
    c[ImGuiCol_TitleBgCollapsed] = hex(P::TOPBAR);
    c[ImGuiCol_DockingEmptyBg] = hex(P::EDITOR_BORDER);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);

    c[ImGuiCol_FrameBg] = hex(P::NUM);
    c[ImGuiCol_FrameBgHovered] = hex(P::NUM_HOVER);
    c[ImGuiCol_FrameBgActive] = hex(P::NUM_ACTIVE);
    c[ImGuiCol_CheckboxSelectedBg] = hex(P::ACCENT);
    c[ImGuiCol_InputTextCursor] = hex(P::TEXT_CURSOR);

    c[ImGuiCol_Border] = hex(P::OUTLINE);
    c[ImGuiCol_BorderShadow] = hex(P::EMBOSS);
    c[ImGuiCol_Separator] = hex(P::SEPARATOR);
    c[ImGuiCol_SeparatorHovered] = hex(P::ACCENT_HOVER);
    c[ImGuiCol_SeparatorActive] = hex(P::ACCENT);
    c[ImGuiCol_TreeLines] = hex(P::OUTLINE);

    c[ImGuiCol_ResizeGrip] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ResizeGripHovered] = hex(P::ACCENT_HOVER);
    c[ImGuiCol_ResizeGripActive] = hex(P::ACCENT);

    c[ImGuiCol_ScrollbarBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab] = hex(P::SCROLL);
    c[ImGuiCol_ScrollbarGrabHovered] = hex(P::SCROLL_HOVER);
    c[ImGuiCol_ScrollbarGrabActive] = hex(P::SCROLL_ACTIVE);

    c[ImGuiCol_Header] = hex(P::LIST_SELECTED);
    c[ImGuiCol_HeaderHovered] = hex(P::LIST_HOVER);
    c[ImGuiCol_HeaderActive] = hex(P::LIST_ACTIVE);
    c[ImGuiCol_Button] = hex(P::NUM);
    c[ImGuiCol_ButtonHovered] = hex(P::NUM_HOVER);
    c[ImGuiCol_ButtonActive] = hex(P::ACCENT);
    c[ImGuiCol_CheckMark] = hex(P::TEXT_HI);
    c[ImGuiCol_SliderGrab] = hex(P::ACCENT);
    c[ImGuiCol_SliderGrabActive] = hex(P::ACCENT_HOVER);

    c[ImGuiCol_Tab] = hex(P::TAB);
    c[ImGuiCol_TabHovered] = hex(P::TAB_HOVER);
    c[ImGuiCol_TabSelected] = hex(P::TAB_ACTIVE);
    c[ImGuiCol_TabSelectedOverline] = hex(P::ACCENT);
    c[ImGuiCol_TabDimmed] = hex(P::TAB);
    c[ImGuiCol_TabDimmedSelected] = hex(P::TAB_ACTIVE);
    c[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    c[ImGuiCol_DockingPreview] = hex(P::ACCENT_SELECT);
    c[ImGuiCol_DragDropTarget] = hex(P::ACCENT);
    c[ImGuiCol_DragDropTargetBg] = hex(P::ACCENT_DROP);
    c[ImGuiCol_NavCursor] = hex(P::ACCENT);
    c[ImGuiCol_NavWindowingHighlight] = hex(P::ACCENT_HOVER);

    c[ImGuiCol_PlotLines] = hex(P::TEXT_DIM);
    c[ImGuiCol_PlotLinesHovered] = hex(P::ACCENT);
    c[ImGuiCol_PlotHistogram] = hex(P::ACCENT);
    c[ImGuiCol_PlotHistogramHovered] = hex(P::ACCENT_HOVER);

    c[ImGuiCol_TableHeaderBg] = hex(P::PANEL);
    c[ImGuiCol_TableBorderStrong] = hex(P::EDITOR_BORDER);
    c[ImGuiCol_TableBorderLight] = hex(P::OUTLINE);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = hex(P::ROW_ALTERNATE);

    UiStyle& ui = ui_style();
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
