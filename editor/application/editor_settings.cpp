#include <editor/application/editor_settings.h>
#include <drivers/windows/window_driver_win32.h>
#include <editor/ui/ui_style.h>
#include <core/base/color.h>
#include <cmath>
#include <iterator>

namespace lumen {

static float _luminance(const ImVec4& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
static ImVec4 _fade(const ImVec4& c, float a) { return { c.x, c.y, c.z, a }; }
static ImVec4 _mix(const ImVec4& a, const ImVec4& b, float t) { return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w }; }

static ImVec4 _shade(const ImVec4& c, float f)
{
    float lum = _luminance(c);
    float t = (lum - 0.35f) / 0.30f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    t = t * t * (3.0f - 2.0f * t);
    float k = f + (1.0f / f - f) * t;
    return { c.x * k, c.y * k, c.z * k, c.w };
}

void Theme::apply() const
{
    ImVec4 acc = accent;
    if (use_system_accent) {
        float r, g, b;
        if (drivers::WindowDriverWin32::system_accent_color(r, g, b)) acc = ImVec4(r, g, b, 1.0f);
    }

    ImGui::StyleColorsDark();
    ImVec4* c = ImGui::GetStyle().Colors;

    c[ImGuiCol_Text] = text;
    c[ImGuiCol_TextDisabled] = _mix(text, base, 0.55f);
    c[ImGuiCol_TextLink] = acc;
    c[ImGuiCol_TextSelectedBg] = _fade(acc, 0.35f);
    c[ImGuiCol_UnsavedMarker] = text;

    // Surfaces
    const ImVec4 chrome = _mix(base, text, 0.012f);
    c[ImGuiCol_WindowBg] = chrome;
    c[ImGuiCol_ChildBg] = chrome;
    c[ImGuiCol_PopupBg] = _shade(base, 0.85f);
    c[ImGuiCol_MenuBarBg] = chrome;
    c[ImGuiCol_TitleBg] = _shade(base, 0.80f);
    c[ImGuiCol_TitleBgActive] = _shade(base, 0.90f);
    c[ImGuiCol_TitleBgCollapsed] = _shade(base, 0.70f);
    c[ImGuiCol_DockingEmptyBg] = _shade(base, 0.70f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.45f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);

    // Frames (inputs, checkboxes, combos)
    c[ImGuiCol_FrameBg] = _mix(_shade(base, 1.25f), acc, 0.06f);
    c[ImGuiCol_FrameBgHovered] = _mix(_shade(base, 1.45f), acc, 0.18f);
    c[ImGuiCol_FrameBgActive] = _mix(_shade(base, 1.60f), acc, 0.30f);
    c[ImGuiCol_CheckboxSelectedBg] = _mix(_shade(base, 1.60f), acc, 0.25f);
    c[ImGuiCol_InputTextCursor] = text;

    // Lines
    c[ImGuiCol_Border] = _shade(base, 1.80f);
    c[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_Separator] = _shade(base, 1.80f);
    c[ImGuiCol_SeparatorHovered] = _fade(acc, 0.70f);
    c[ImGuiCol_SeparatorActive] = acc;
    c[ImGuiCol_TreeLines] = _shade(base, 1.60f);

    // Window resize grip
    c[ImGuiCol_ResizeGrip] = _fade(acc, 0.20f);
    c[ImGuiCol_ResizeGripHovered] = _fade(acc, 0.50f);
    c[ImGuiCol_ResizeGripActive] = acc;

    // Scrollbar
    c[ImGuiCol_ScrollbarBg] = _shade(base, 0.80f);
    c[ImGuiCol_ScrollbarGrab] = _shade(base, 1.60f);
    c[ImGuiCol_ScrollbarGrabHovered] = _fade(acc, 0.50f);
    c[ImGuiCol_ScrollbarGrabActive] = acc;

    // Interactive
    c[ImGuiCol_Header] = _fade(acc, 0.35f);
    c[ImGuiCol_HeaderHovered] = _fade(acc, 0.55f);
    c[ImGuiCol_HeaderActive] = acc;
    c[ImGuiCol_Button] = _fade(acc, 0.30f);
    c[ImGuiCol_ButtonHovered] = _fade(acc, 0.50f);
    c[ImGuiCol_ButtonActive] = acc;
    c[ImGuiCol_CheckMark] = acc;
    c[ImGuiCol_SliderGrab] = acc;
    c[ImGuiCol_SliderGrabActive] = acc;

    // Tabs
    c[ImGuiCol_Tab] = _shade(base, 0.90f);
    c[ImGuiCol_TabHovered] = _fade(acc, 0.60f);
    c[ImGuiCol_TabSelected] = _fade(acc, 0.80f);
    c[ImGuiCol_TabSelectedOverline] = acc;
    c[ImGuiCol_TabDimmed] = _shade(base, 0.85f);
    c[ImGuiCol_TabDimmedSelected] = _shade(base, 1.05f);
    c[ImGuiCol_TabDimmedSelectedOverline] = _fade(acc, 0.40f);

    // Docking / drag-drop / nav
    c[ImGuiCol_DockingPreview] = _fade(acc, 0.60f);
    c[ImGuiCol_DragDropTarget] = acc;
    c[ImGuiCol_DragDropTargetBg] = _fade(acc, 0.15f);
    c[ImGuiCol_NavCursor] = acc;
    c[ImGuiCol_NavWindowingHighlight] = _fade(acc, 0.70f);

    // Plots
    c[ImGuiCol_PlotLines] = _mix(text, base, 0.35f);
    c[ImGuiCol_PlotLinesHovered] = acc;
    c[ImGuiCol_PlotHistogram] = _fade(acc, 0.75f);
    c[ImGuiCol_PlotHistogramHovered] = acc;

    // Tables
    c[ImGuiCol_TableHeaderBg] = _shade(base, 1.20f);
    c[ImGuiCol_TableBorderStrong] = _shade(base, 1.90f);
    c[ImGuiCol_TableBorderLight] = _shade(base, 1.50f);
    c[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.03f);

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

const char* Theme::theme_preset_name(int i)
{
    return (i < 0 || i >= (int)std::size(THEME_PRESETS)) ? "Custom" : THEME_PRESETS[i].name;
}

int Theme::theme_preset_index(std::string_view n)
{
    for (int i = 0; i < (int)std::size(THEME_PRESETS); ++i) if (n == THEME_PRESETS[i].name) return i;
    return -1;
}

}
