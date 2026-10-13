#include <drivers/imgui/ui_style.h>

namespace lumen {

static ImU32 _mix(const ImVec4& p_a, const ImVec4& p_b, float p_t)
{
    return ImGui::GetColorU32(ImVec4(p_a.x + (p_b.x - p_a.x) * p_t, p_a.y + (p_b.y - p_a.y) * p_t, p_a.z + (p_b.z - p_a.z) * p_t, 1.0f));
}

UiStyle& ui_style()
{
    static UiStyle s;
    return s;
}

ImU32 ui_with_alpha(ImU32 p_color, float p_alpha)
{
    const ImU32 a = (ImU32)(p_alpha * 255.0f + 0.5f);
    return (p_color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}

ImVec4 ui_vec4(ImU32 p_color)
{
    return ImGui::ColorConvertU32ToFloat4(p_color);
}

void UiStyle::build()
{
    _build_components();
    _build_colors();
}

void UiStyle::_build_components()
{
    base.window_padding = ImVec2(space.lg, space.lg);
    base.frame_padding = ImVec2(space.sm, space.xs);
    base.item_spacing = ImVec2(space.lg, space.sm);
    base.item_inner_spacing = ImVec2(space.sm, space.sm);
    base.cell_padding = ImVec2(space.sm, space.xxs);
    base.indent = 21.0f;
    base.scrollbar_size = 14.0f;
    base.scrollbar_rounding = 9.0f;
    base.grab_min_size = space.xxl;
    base.window_rounding = radius.none;
    base.frame_rounding = radius.none;
    base.popup_rounding = radius.none;
    base.grab_rounding = radius.none;
    base.tab_rounding = radius.lg;
    base.window_border = stroke.hairline;
    base.popup_border = stroke.hairline;
    base.frame_border = 0.0f;

    dock.gap = space.sm;
    dock.panel_padding = ImVec2(space.xl, space.lg);
    dock.panel_scrollbar = space.xl;
    dock.drawer_padding = ImVec2(space.lg, space.lg);
    dock.drop_outline = stroke.thick;

    tab_strip.height_pad = space.xl;
    tab_strip.pad_x = space.xxl;
    tab_strip.min_width = 36.0f;
    tab_strip.accent_height = stroke.thick;

    dock_tab.inset_x = space.sm;
    dock_tab.inset_top = space.xs;
    dock_tab.gap = space.xxs;
    dock_tab.rounding = radius.md;
    dock_tab.close_pad = space.md;
    dock_tab.close_size_pad = space.xxs;
    dock_tab.close_rounding = radius.sm;
    dock_tab.close_glyph_scale = 0.22f;
    dock_tab.close_glyph_stroke = stroke.thin;
    dock_tab.label_min_pad = space.md;
    dock_tab.label_trail = " ";

    menu.padding = ImVec2(space.sm, space.sm);
    menu.rounding = radius.none;
    menu.border = stroke.hairline;
    menu.popup_offset = space.xxs;

    splitter.line_width = stroke.thick;
    splitter.grip_half_width = stroke.thick;
    splitter.grip_length = 40.0f;

    property.label_ratio = 0.4f;
    property.label_ratio_wide = 0.55f;
    property.row_label_width = 200.0f;
    property.section_gap = space.xxs;

    toolbar.height = 36.0f;
    toolbar.padding_x = space.lg;

    play.margin = space.md;
    play.inset = space.xxs;
    play.rounding = radius.sm;
    play.button_extra_width = space.sm;
    play.kebab_width = 14.0f;
    play.kebab_gap = space.sm;

    titlebar.menu_padding = ImVec2(space.xxl, 7.0f);
    titlebar.tab_height = 24.0f;
    titlebar.tab_pad_x = 14.0f;
    titlebar.tab_rounding = radius.xl;
    titlebar.tab_inner_spacing = space.xxs;
    titlebar.caption_width = 46.0f;
    titlebar.caption_glyph = 5.0f;
    titlebar.caption_glyph_offset = space.xxs;
    titlebar.caption_stroke = stroke.hairline;
    titlebar.caption_close_stroke = 1.2f;
    titlebar.logo_margin = space.md;
    titlebar.logo_rounding = radius.md;
    titlebar.menu_offset_x = space.md;
    titlebar.title_margin = space.xxxl;
    titlebar.stats_margin = space.xl;

    overlay.margin = space.lg;
    overlay.spacing = space.sm;
    overlay.group_gap = 5.0f;
    overlay.rounding = radius.pill;

    view_menu.indicator_x = space.xxl;
    view_menu.indicator_radius = 5.0f;
    view_menu.indicator_dot_radius = 2.5f;
    view_menu.indicator_stroke = stroke.thin;
    view_menu.label_x = 28.0f;
    view_menu.icon_slot_scale = 1.4f;
    view_menu.icon_gap = space.sm;
    view_menu.arrow_inset = space.sm;

    outliner.pad_x = space.xl;
    outliner.row_pad_y = space.xs;
    outliner.cell_pad_x = space.sm;
    outliner.icon_width_scale = 1.25f;
    outliner.icon_gap = space.sm;
    outliner.rename_pad_x = space.xxs;
    outliner.arrow_scale = 0.7f;
    outliner.arrow_offset_scale = 0.15f;
    outliner.padding = ImVec2(0.0f, space.lg);
    outliner.label_column_weight = 0.6f;
    outliner.type_column_weight = 0.4f;

    details.card_frame_padding = ImVec2(space.xs, space.xs);
    details.accent_bar = stroke.thick;

    list_row.height = 48.0f;
    list_row.spacing = space.sm;
    list_row.rounding = radius.lg;
    list_row.pad = space.lg;
    list_row.thumb_rounding = radius.md;
    list_row.star_stroke = stroke.thin;
    list_row.subtitle_scale = 0.85f;
    list_row.subtitle_gap = space.xs;

    project_manager.sort_width = 160.0f;
    project_manager.actions_width = 150.0f;
    project_manager.modal_width = 520.0f;
    project_manager.browse_width = 90.0f;
    project_manager.button_width = 120.0f;

    asset_browser.row_pad_y = space.sm;
    asset_browser.chevron_width = 18.0f;
    asset_browser.icon_gap = space.md;
    asset_browser.crumb_gap = space.sm;
    asset_browser.item_icon = 36.0f;
    asset_browser.item_pad = space.md;
    asset_browser.item_text_top = 7.0f;
    asset_browser.item_subtitle_gap = stroke.hairline;
    asset_browser.progress_height = stroke.thick;
    asset_browser.grid_spacing = ImVec2(space.xxxl, space.lg);
    asset_browser.splitter = space.md;
    asset_browser.min_side = 120.0f;
    asset_browser.divider_shadow = space.huge;

    profiler.lane_gap = space.xs;
    profiler.section_height = 18.0f;
    profiler.pass_height = 22.0f;
    profiler.barrier_height = space.lg;
    profiler.draw_height = 14.0f;
    profiler.bar_inset = stroke.hairline;
    profiler.bar_rounding = radius.sm;
    profiler.label_pad = space.sm;
    profiler.label_min_width = space.md;
    profiler.ruler_text_pad = space.xs;
    profiler.status_pad = ImVec2(space.xxs, stroke.hairline);
    profiler.cursor_stroke = stroke.hairline;
    profiler.side_panel_ratio = 0.2f;
    profiler.plot_aspect = 2.0f / 3.0f;

    data_table.column_xs = 50.0f;
    data_table.column_sm = 60.0f;
    data_table.column_md = 70.0f;
    data_table.column_lg = 90.0f;
    data_table.button_width = 70.0f;
    data_table.bar_rounding = radius.sm;
    data_table.block_gap = space.sm;

    console_bar.pad = space.lg;
    console_bar.field_width = 360.0f;
    console_bar.field_min_width = 120.0f;
    console_bar.rounding = radius.sm;
}

void UiStyle::_build_colors()
{
    const ImVec4* c = ImGui::GetStyle().Colors;
    const ImVec4 bg = c[ImGuiCol_WindowBg];
    const ImVec4 fg = c[ImGuiCol_Text];
    const ImVec4 chrome = ui_vec4(_mix(bg, fg, surface_mix.chrome));
    const ImVec4 accent = c[ImGuiCol_TabSelectedOverline];

    color.sunken = _mix(bg, fg, surface_mix.sunken);
    color.chrome = _mix(bg, fg, surface_mix.chrome);
    color.hover = _mix(bg, fg, surface_mix.hover);
    color.panel = _mix(bg, fg, surface_mix.panel);
    color.field = _mix(bg, fg, surface_mix.field);
    color.line = _mix(bg, fg, surface_mix.line);
    color.field_hover = _mix(bg, fg, surface_mix.field_hover);
    color.field_active = _mix(bg, fg, surface_mix.field_active);
    color.field_pressed = _mix(bg, fg, surface_mix.field_pressed);

    color.text = ImGui::GetColorU32(fg);
    color.text_dim = ImGui::GetColorU32(c[ImGuiCol_TextDisabled]);

    color.accent = ImGui::GetColorU32(accent);
    color.accent_soft = _mix(chrome, accent, accent_mix.soft);
    color.accent_soft_hover = _mix(chrome, accent, accent_mix.soft_hover);
    color.accent_soft_active = _mix(chrome, accent, accent_mix.soft_active);
    color.selection = ui_with_alpha(color.accent, alpha.selection);

    color.drop_fill = ImGui::GetColorU32(c[ImGuiCol_DragDropTargetBg]);
    color.drop_line = ImGui::GetColorU32(c[ImGuiCol_DragDropTarget]);

    color.success = IM_COL32(29, 124, 18, 255);
    color.warning = IM_COL32(209, 168, 69, 255);
    color.danger = IM_COL32(219, 89, 89, 255);
    color.danger_strong = IM_COL32(196, 43, 28, 255);
    color.danger_soft = IM_COL32(153, 38, 38, 153);
    color.danger_soft_active = IM_COL32(191, 51, 51, 204);
    color.notice = IM_COL32(230, 140, 102, 255);
    color.favorite = IM_COL32(255, 180, 0, 255);
    color.folder = IM_COL32(224, 187, 88, 255);

    color.overlay = IM_COL32(28, 28, 30, 130);
    color.overlay_hover = IM_COL32(60, 62, 66, 190);
    color.overlay_active = IM_COL32(80, 82, 88, 220);
    color.overlay_label = IM_COL32(20, 20, 20, 180);

    color.shadow = IM_COL32(0, 0, 0, 80);
    color.shadow_text = IM_COL32(0, 0, 0, 140);
    color.transparent = IM_COL32(0, 0, 0, 0);

    color.viz_neutral = IM_COL32(179, 179, 179, 255);
    color.viz_barrier = IM_COL32(90, 130, 180, 220);
    color.viz_hovered = color.danger;
    color.viz_cursor = IM_COL32(255, 255, 255, 120);
    color.viz_stripe_even = IM_COL32(45, 45, 45, 255);
    color.viz_stripe_odd = IM_COL32(55, 55, 55, 255);
    color.viz_label = IM_COL32(220, 220, 220, 180);
    color.viz_ok = IM_COL32(88, 180, 120, 255);
    color.viz_warn = color.warning;
    color.viz_full = color.danger;
}

void UiStyle::apply_to_imgui() const
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = base.window_padding;
    s.FramePadding = base.frame_padding;
    s.ItemSpacing = base.item_spacing;
    s.ItemInnerSpacing = base.item_inner_spacing;
    s.CellPadding = base.cell_padding;
    s.IndentSpacing = base.indent;
    s.ScrollbarSize = base.scrollbar_size;
    s.ScrollbarRounding = base.scrollbar_rounding;
    s.GrabMinSize = base.grab_min_size;
    s.WindowRounding = base.window_rounding;
    s.ChildRounding = base.window_rounding;
    s.FrameRounding = base.frame_rounding;
    s.PopupRounding = base.popup_rounding;
    s.GrabRounding = base.grab_rounding;
    s.TabRounding = base.tab_rounding;
    s.WindowBorderSize = base.window_border;
    s.ChildBorderSize = base.window_border;
    s.PopupBorderSize = base.popup_border;
    s.FrameBorderSize = base.frame_border;
}

}
