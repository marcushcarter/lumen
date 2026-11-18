#include <editor/ui/ui_style.h>
#include <core/base/color.h>
#include <imgui_internal.h>

namespace lumen {

static ImU32 _mix(const ImVec4& p_a, const ImVec4& p_b, float p_t)
{
    return ImGui::GetColorU32(ImVec4(p_a.x + (p_b.x - p_a.x) * p_t, p_a.y + (p_b.y - p_a.y) * p_t, p_a.z + (p_b.z - p_a.z) * p_t, 1.0f));
}

const UiStyle::Palette& UiStyle::palette_dark()
{
    static const Palette p = [] {
        Palette d;
        d.EDITOR_BORDER = 0x3c3c3cff;
        d.TOPBAR = 0x2d2d30ff;
        d.BACK = 0x1b1b1bff;
        d.HEADER = 0x2d2d30ff;
        d.OUTLINER_BACK = 0x1b1b1bff;
        d.VIEWPORT_BACK = 0x262626ff;
        d.PANEL = 0x2a2a2aff;
        d.PANEL_SUB = 0x0000001f;
        d.PANEL_OUTLINE = 0xffffff0d;
        d.OUTLINE = 0x434346ff;
        d.SEPARATOR = 0xffffff1f;
        d.EMBOSS = 0x00000026;
        d.TEXT = 0xe8e8e8ff;
        d.TEXT_HI = 0xffffffff;
        d.TEXT_DIM = 0x8c8c8cff;
        d.TEXT_STATUS = 0x9b9b9bff;
        d.TEXT_CURSOR = 0xffffffff;
        d.LINK = 0x5ea8f0ff;
        d.NUM = 0x252526ff;
        d.NUM_HOVER = 0x2d2d30ff;
        d.NUM_ACTIVE = 0x1e1e1eff;
        d.TEXT_FIELD = 0x252526ff;
        d.TEXT_FIELD_HOVER = 0x2d2d30ff;
        d.TEXT_FIELD_ACTIVE = 0x1e1e1eff;
        d.MENU = 0x252526ff;
        d.MENU_HOVER = 0x2d2d30ff;
        d.TOOLBAR_ITEM = 0x2d2d30ff;
        d.PULLDOWN_TEXT = 0xf1f1f1ff;
        d.PULLDOWN_HOVER = 0xffffff1f;
        d.MENU_BACK = 0x1f1f1fff;
        d.MENU_BACK_OUTLINE = 0x4a4a4aff;
        d.MENU_ITEM_TEXT = 0xf1f1f1ff;
        d.MENU_ITEM_DIM = 0x9b9b9bff;
        d.TOOLTIP = 0x1f1f1fff;
        d.TOOLTIP_OUTLINE = 0x434343ff;
        d.ACCENT = 0x0078d7ff;
        d.ACCENT_HOVER = 0x1c97eaff;
        d.ACCENT_SELECT = 0x0078d766;
        d.ACCENT_DROP = 0x0078d733;
        d.TAB = 0x2d2d30ff;
        d.TAB_HOVER = 0x3e3e42ff;
        d.TAB_ACTIVE = 0x1b1b1bff;
        d.LIST_HOVER = 0xffffff10;
        d.LIST_SELECTED = 0x0078d7ff;
        d.LIST_ACTIVE = 0x1c97eaff;
        d.ROW_ALTERNATE = 0xffffff05;
        d.SCROLL = 0x4a4a4aff;
        d.SCROLL_HOVER = 0x5a5a5aff;
        d.SCROLL_ACTIVE = 0x6a6a6aff;
        d.GRID = 0x4a4a4aff;
        d.ICON_FOLDER = 0xe6c364ff;
        d.CHECK = 0xffffffff;
        d.STATS_TEXT = 0xffe14dff;
        d.PLAY_BORDER = 0xf08a24ff;
        d.GRID_LINE = 0x2e2e30ff;
        d.TOOLBAR = 0x1f1f1fff;
        d.SEARCH = 0x252526ff;
        d.SEARCH_BORDER = 0x434346ff;
        d.ACTIVE_ICON = 0xffffffff;
        d.TOOL_HOVER = 0x3e3e42ff;
        d.VIEW_HEADER = 0x2d2d30ff;
        d.VIEW_BAR = 0x1f1f1fff;
        return d;
    }();
    return p;
}

const UiStyle::Palette& UiStyle::palette_light()
{
    static const Palette p = [] {
        Palette l;
        l.EDITOR_BORDER = 0xb4bcc6ff;
        l.TOPBAR = 0xc9d9eaff;
        l.BACK = 0xffffffff;
        l.HEADER = 0xc9d9eaff;
        l.OUTLINER_BACK = 0xffffffff;
        l.VIEWPORT_BACK = 0x9a9a9aff;
        l.PANEL = 0xeeeeeeff;
        l.PANEL_SUB = 0x0000000f;
        l.PANEL_OUTLINE = 0x00000014;
        l.OUTLINE = 0xa9b3bfff;
        l.SEPARATOR = 0x0000001f;
        l.EMBOSS = 0x00000014;
        l.TEXT = 0x000000ff;
        l.TEXT_HI = 0xffffffff;
        l.TEXT_DIM = 0x8a8a8aff;
        l.TEXT_STATUS = 0x7a7a7aff;
        l.TEXT_CURSOR = 0x000000ff;
        l.LINK = 0x0060c0ff;
        l.NUM = 0xffffffff;
        l.NUM_HOVER = 0xf5f9fdff;
        l.NUM_ACTIVE = 0xffffffff;
        l.TEXT_FIELD = 0xffffffff;
        l.TEXT_FIELD_HOVER = 0xf5f9fdff;
        l.TEXT_FIELD_ACTIVE = 0xffffffff;
        l.MENU = 0xffffffff;
        l.MENU_HOVER = 0xe5f3ffff;
        l.TOOLBAR_ITEM = 0xf0f0f0ff;
        l.PULLDOWN_TEXT = 0x111111ff;
        l.PULLDOWN_HOVER = 0xffffff66;
        l.MENU_BACK = 0xf5f5f5ff;
        l.MENU_BACK_OUTLINE = 0x979797ff;
        l.MENU_ITEM_TEXT = 0x111111ff;
        l.MENU_ITEM_DIM = 0x7a7a7aff;
        l.TOOLTIP = 0xffffffff;
        l.TOOLTIP_OUTLINE = 0xa9b5c3ff;
        l.ACCENT = 0x3c91e6ff;
        l.ACCENT_HOVER = 0x5aa5eeff;
        l.ACCENT_SELECT = 0x3b8fe84d;
        l.ACCENT_DROP = 0x3b8fe826;
        l.TAB = 0xc9d9eaff;
        l.TAB_HOVER = 0xe2ebf5ff;
        l.TAB_ACTIVE = 0xffffffff;
        l.LIST_HOVER = 0xe5f3ffff;
        l.LIST_SELECTED = 0xcce8ffff;
        l.LIST_ACTIVE = 0x99d1ffff;
        l.ROW_ALTERNATE = 0x00000008;
        l.SCROLL = 0xc2c2c2ff;
        l.SCROLL_HOVER = 0xa8a8a8ff;
        l.SCROLL_ACTIVE = 0x8a8a8aff;
        l.GRID = 0x4a4a4aff;
        l.ICON_FOLDER = 0xe0b84aff;
        l.CHECK = 0x3b8fe8ff;
        l.STATS_TEXT = 0xffe14dff;
        l.PLAY_BORDER = 0xf08a24ff;
        l.GRID_LINE = 0xe2e2e2ff;
        l.TOOLBAR = 0xf5f6f7ff;
        l.SEARCH = 0xffffffff;
        l.SEARCH_BORDER = 0xa9b3bfff;
        l.ACTIVE_ICON = 0x111111ff;
        l.TOOL_HOVER = 0xd5e8f9ff;
        l.VIEW_HEADER = 0xc9d9eaff;
        l.VIEW_BAR = 0xf5f6f7ff;
        return l;
    }();
    return p;
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

static float _channel(uint32_t p_rgba, int p_shift)
{
    return (float)((p_rgba >> p_shift) & 0xff) / 255.0f;
}

ImVec4 ui_hex4(uint32_t p_rgba)
{
    return ImVec4(color_srgb_to_linear(_channel(p_rgba, 24)), color_srgb_to_linear(_channel(p_rgba, 16)), color_srgb_to_linear(_channel(p_rgba, 8)), _channel(p_rgba, 0));
}

ImU32 ui_hex(uint32_t p_rgba)
{
    return ImGui::GetColorU32(ui_hex4(p_rgba));
}

ImVec4 ui_hex4_over(uint32_t p_rgba, uint32_t p_background)
{
    const float a = _channel(p_rgba, 0);
    if (a >= 1.0f) return ui_hex4(p_rgba);
    float fg[3], bg[3], lin_fg[3], lin_bg[3];
    for (int i = 0; i < 3; i++) {
        fg[i] = _channel(p_rgba, 24 - i * 8);
        bg[i] = _channel(p_background, 24 - i * 8);
        lin_fg[i] = color_srgb_to_linear(fg[i]);
        lin_bg[i] = color_srgb_to_linear(bg[i]);
    }
    float num = 0.0f;
    float den = 0.0f;
    for (int i = 0; i < 3; i++) {
        const float target = color_srgb_to_linear(bg[i] + (fg[i] - bg[i]) * a);
        const float d = lin_fg[i] - lin_bg[i];
        num += (target - lin_bg[i]) * d;
        den += d * d;
    }
    const float la = den > 1e-8f ? ImClamp(num / den, 0.0f, 1.0f) : a;
    return ImVec4(lin_fg[0], lin_fg[1], lin_fg[2], la);
}

ImU32 ui_hex_over(uint32_t p_rgba, uint32_t p_background)
{
    return ImGui::GetColorU32(ui_hex4_over(p_rgba, p_background));
}

void UiStyle::build()
{
    pal = light ? palette_light() : palette_dark();
    unit = ImFloor(16.0f * scale);
    row = ImFloor(20.0f * scale);
    font.size = ImFloor(13.0f * scale);
    font.icon_size = ImFloor(font.size * 0.9f);
    space.xxs = ImFloor(1.0f * scale);
    space.xs = ImFloor(2.0f * scale);
    space.sm = ImFloor(3.0f * scale);
    space.md = ImFloor(4.0f * scale);
    space.lg = ImFloor(6.0f * scale);
    space.xl = ImFloor(8.0f * scale);
    space.xxl = ImFloor(10.0f * scale);
    space.xxxl = ImFloor(12.0f * scale);
    space.huge = ImFloor(16.0f * scale);
    radius.sm = 0.0f;
    radius.md = 0.0f;
    radius.lg = 0.0f;
    radius.xl = 0.0f;
    _build_components();
    _build_colors();
}

void UiStyle::_build_components()
{
    base.window_padding = ImVec2(space.md, space.md);
    base.frame_padding = ImVec2(space.md, ImFloor((row - font.size) * 0.5f));
    base.item_spacing = ImVec2(space.md, space.xs);
    base.item_inner_spacing = ImVec2(space.sm, space.xs);
    base.cell_padding = ImVec2(space.sm, 1.0f);
    base.indent = ImFloor(14.0f * scale);
    base.scrollbar_size = ImFloor(12.0f * scale);
    base.scrollbar_rounding = radius.md;
    base.grab_min_size = ImFloor(12.0f * scale);
    base.window_rounding = radius.md;
    base.frame_rounding = radius.md;
    base.popup_rounding = radius.md;
    base.grab_rounding = radius.md;
    base.tab_rounding = radius.md;
    base.window_border = stroke.hairline;
    base.popup_border = stroke.hairline;
    base.frame_border = stroke.hairline;




    menu.padding = ImVec2(space.md, space.sm);
    menu.rounding = radius.md;
    menu.border = stroke.hairline;
    menu.popup_offset = space.xxs;

    splitter.gap = 0.0f;
    splitter.hit = ImFloor(3.0f * scale);
    splitter.line_width = stroke.hairline;
    splitter.grip_half_width = stroke.thick;
    splitter.grip_length = 0.0f;

    property.label_ratio = 0.4f;
    property.label_ratio_wide = 0.55f;
    property.row_label_width = 200.0f;
    property.section_gap = space.xxs;

    toolbar.height = row;
    toolbar.main_height = ImFloor(24.0f * scale) * 2.0f;
    toolbar.padding_x = space.lg;

    header.height = toolbar.height;
    header.pad_x = space.sm;
    header.item_gap = space.xxs;
    header.group_gap = space.lg;
    header.rounding = radius.md;
    header.menu_pad_x = space.md;
    header.dropdown_arrow = ImFloor(font.size * 0.32f);

    panel_box.margin_x = 0.0f;
    panel_box.margin_top = 0.0f;
    panel_box.gap = 0.0f;
    panel_box.rounding = radius.md;
    panel_box.header_height = row;
    panel_box.body_pad_y = space.xs;
    panel_box.grip_dot = 1.0f;
    panel_box.grip_spacing = 3.0f;


    panel.padding = ImVec2(space.xs, space.xs);
    panel.header_height = ImFloor(22.0f * scale);
    panel.header_pad_x = space.md;

    viewport.play_border = ImFloor(2.0f * scale);

    titlebar.height = ImFloor(30.0f * scale);
    titlebar.search_width = ImFloor(170.0f * scale);
    titlebar.menu_padding = ImVec2(space.md, space.xs);
    titlebar.menu_gap = 0.0f;
    titlebar.menu_height = unit + space.sm;
    titlebar.tab_inset_x = 0.0f;
    titlebar.tab_height = unit + space.xxs;
    titlebar.tab_top_space = 0.0f;
    titlebar.tab_pad_x = space.lg;
    titlebar.tab_rounding = radius.md;
    titlebar.tab_inner_spacing = space.xxs;
    titlebar.caption_width = 46.0f;
    titlebar.caption_glyph = 5.0f;
    titlebar.caption_glyph_offset = space.xxs;
    titlebar.caption_stroke = stroke.hairline;
    titlebar.caption_close_stroke = 1.2f;
    titlebar.logo_margin = ImFloor(5.0f * scale);
    titlebar.logo_rounding = radius.md;
    titlebar.menu_offset_x = space.md;
    titlebar.title_margin = space.xxxl;
    titlebar.stats_margin = space.xl;

    overlay.margin = space.sm;
    overlay.spacing = space.xxs;
    overlay.group_gap = space.lg;
    overlay.rounding = radius.md;

    timeline.padding_x = space.md;
    timeline.gap = space.lg;
    timeline.track_thickness = stroke.thick;
    timeline.knob_radius = 6.0f;


    outliner.pad_x = space.sm;
    outliner.row_pad_y = ImFloor((ImFloor(18.0f * scale) - font.size) * 0.5f);
    outliner.cell_pad_x = space.sm;
    outliner.icon_width_scale = 1.2f;
    outliner.icon_gap = space.md;
    outliner.box_gap = space.md;
    outliner.rename_pad_x = space.xxs;
    outliner.arrow_scale = 0.7f;
    outliner.arrow_offset_scale = 0.15f;
    outliner.padding = ImVec2(0.0f, space.xs);


    details.header_pad_y = ImFloor(2.0f * scale);
    details.header_indent = ImFloor(18.0f * scale);
    details.button_inset = space.md;
    details.arrow_x = space.md;
    details.arrow_scale = 0.6f;
    details.line = stroke.hairline;
    details.axis_tab_width = ImFloor(font.size);
    details.axis_gap = 0.0f;

    list_row.height = 48.0f;
    list_row.spacing = space.sm;
    list_row.rounding = radius.lg;
    list_row.pad = space.lg;
    list_row.thumb_rounding = radius.md;
    list_row.star_stroke = stroke.thin;
    list_row.subtitle_scale = 0.85f;
    list_row.subtitle_gap = space.xs;

    project_manager.inspector_width = 300.0f;
    project_manager.filter_width = 300.0f;
    project_manager.sort_width = 160.0f;
    project_manager.chip_pad_x = space.xxl;
    project_manager.row_height = 56.0f;
    project_manager.row_pad_x = space.xxxl;
    project_manager.row_gap = space.xxl;
    project_manager.row_accent_bar = stroke.thick;
    project_manager.thumb_size = 40.0f;
    project_manager.thumb_rounding = radius.md;
    project_manager.hero_height = 170.0f;
    project_manager.inspector_padding = ImVec2(space.xxxl, space.xxxl);
    project_manager.title_font_size = 18.0f;
    project_manager.action_height = 34.0f;
    project_manager.action_gap = space.lg;
    project_manager.modal_width = 520.0f;
    project_manager.browse_width = 90.0f;
    project_manager.button_width = 120.0f;

    // import_dialog.width = 440.0f;
    // import_dialog.height = 560.0f;
    // import_dialog.min_width = 360.0f;
    // import_dialog.min_height = 320.0f;
    // import_dialog.button_width = 96.0f;
    settings.nav_width = 200.0f;
    settings.nav_item_height = unit;
    settings.content_padding = space.huge;
    settings.content_max_width = 720.0f;

    asset_tooltip.thumbnail_size = 144.0f;
    asset_picker.width = 320.0f;
    asset_picker.max_height = 420.0f;
    asset_picker.thumbnail_size = 32.0f;
    asset_tooltip.thumbnail_rounding = 0.0f;
    asset_tooltip.path_wrap_width = 280.0f;
    bones.joint_radius = 3.0f;
    bones.bone_thickness = 1.5f;

    import_dialog.width = project_manager.modal_width;
    import_dialog.max_height_ratio = 0.85f;
    import_dialog.button_width = project_manager.button_width;

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
    asset_browser.layout_flip_ratio = 1.15f;
    asset_browser.card_width = 96.0f;
    asset_browser.card_gap = space.lg;
    asset_browser.card_pad = space.md;
    asset_browser.card_icon_scale = 2.5f;
    asset_browser.list_row_pad = space.xs;
    asset_browser.list_icon_scale = 0.62f;

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
    profiler.lane_header_height = 16.0f;
    profiler.zone_height = 18.0f;
    profiler.section_gap = space.lg;
    profiler.marker_stroke = stroke.hairline;

    data_table.column_xs = 50.0f;
    data_table.column_sm = 60.0f;
    data_table.column_md = 70.0f;
    data_table.column_lg = 90.0f;
    data_table.button_width = 70.0f;
    data_table.bar_rounding = radius.sm;
    data_table.block_gap = space.sm;


    gizmo.arrow_ratio = 0.22f;
    gizmo.ring_scale = 0.63f;
    gizmo.scale_handle_scale = 1.3f;
    gizmo.axis_thickness = 3.0f;
    gizmo.ring_thickness = 2.25f;
    gizmo.head_length = 14.0f;
    gizmo.head_width = 11.0f;
    gizmo.handle_size = 12.0f;
    gizmo.center_radius = 6.5f;
    gizmo.label_offset = space.xl;
    gizmo.hit_tolerance = space.md;
    gizmo.drag_alpha = 0.7f;
}

void UiStyle::_build_colors()
{
    const Palette& p = pal;

    color.editor_border = ui_hex(p.EDITOR_BORDER);
    color.topbar = ui_hex(p.TOPBAR);
    color.topbar_hover = ui_hex_over(p.PULLDOWN_HOVER, p.TOPBAR);
    color.header = ui_hex(p.HEADER);
    color.header_hover = ui_hex_over(p.PULLDOWN_HOVER, p.HEADER);
    color.outliner_back = ui_hex(p.OUTLINER_BACK);
    color.viewport_back = ui_hex(p.VIEWPORT_BACK);
    color.grid_line = ui_hex(p.GRID_LINE);
    color.search_match = ui_hex(0xfff200ff);
    color.panel_sub = ui_hex_over(p.PANEL_SUB, p.PANEL);
    color.outline = ui_hex(p.OUTLINE);
    color.emboss = ImGui::GetColorU32(ui_hex4_over(p.EMBOSS, p.BACK));
    color.text_field = ui_hex(p.TEXT_FIELD);
    color.text_field_hover = ui_hex(p.TEXT_FIELD_HOVER);
    color.text_field_active = ui_hex(p.TEXT_FIELD_ACTIVE);
    color.menu_field = ui_hex(p.MENU);
    color.menu_field_hover = ui_hex(p.MENU_HOVER);
    color.menu_back = ui_hex(p.MENU_BACK);
    color.menu_outline = ui_hex(p.MENU_BACK_OUTLINE);
    color.menu_text = ui_hex(p.MENU_ITEM_TEXT);
    color.menu_dim = ui_hex(p.MENU_ITEM_DIM);
    color.pulldown_text = ui_hex(p.PULLDOWN_TEXT);
    color.tab = ui_hex(p.TAB);
    color.tab_hover = ui_hex(p.TAB_HOVER);
    color.tab_active = ui_hex(p.TAB_ACTIVE);
    color.list_hover = ui_hex_over(p.LIST_HOVER, p.BACK);
    color.list_selected = ui_hex(p.LIST_SELECTED);
    color.list_active = ui_hex(p.LIST_ACTIVE);
    color.row_alternate = ui_hex_over(p.ROW_ALTERNATE, p.OUTLINER_BACK);

    color.sunken = color.editor_border;
    color.chrome = color.topbar;
    color.hover = color.topbar_hover;
    color.panel = ui_hex(p.BACK);
    color.field = ui_hex(p.NUM);
    color.line = color.outline;
    color.field_hover = ui_hex(p.NUM_HOVER);
    color.field_active = ui_hex(p.NUM_ACTIVE);
    color.field_pressed = ui_hex(p.ACCENT);

    color.text = ui_hex(p.TEXT);
    color.text_hi = ui_hex(p.TEXT_HI);
    color.text_dim = ui_hex(p.TEXT_DIM);

    const ImVec4 chrome = ui_vec4(color.chrome);
    const ImVec4 accent = ui_hex4(p.ACCENT);
    const ImVec4 fg = ui_hex4(p.TEXT);
    color.accent = ui_hex(p.ACCENT);
    color.accent_soft = _mix(chrome, accent, 0.18f);
    color.accent_soft_hover = _mix(chrome, accent, 0.30f);
    color.accent_soft_active = _mix(chrome, accent, 0.42f);
    color.accent_hover = ui_hex(p.ACCENT_HOVER);
    color.accent_active = _mix(accent, fg, 0.24f);
    color.selection = ui_hex(p.ACCENT_SELECT);

    color.drop_fill = ui_hex(p.ACCENT_DROP);
    color.drop_line = ui_hex(p.ACCENT);

    color.success = ui_hex(p.STATE_SUCCESS);
    color.success_soft = _mix(chrome, ui_vec4(color.success), 0.30f);
    color.success_soft_hover = _mix(chrome, ui_vec4(color.success), 0.42f);
    color.success_soft_active = _mix(chrome, ui_vec4(color.success), 0.54f);
    color.success_text = ui_hex(0x6cd27aff);
    color.warning = ui_hex(p.STATE_WARNING);
    color.danger = ui_hex(0xe63a3aff);
    color.danger_strong = ui_hex(0xc42b1cff);
    color.danger_soft = ImGui::GetColorU32(ui_hex4_over(0xe6000066, p.BACK));
    color.danger_soft_active = ImGui::GetColorU32(ui_hex4_over(0xe60000a0, p.BACK));
    color.notice = ui_hex(0xcc7529ff);
    color.favorite = ui_hex(0xffb400ff);
    color.folder = ui_hex(p.ICON_FOLDER);
    color.asset_model = IM_COL32(86, 182, 230, 255);
    color.asset_mesh = IM_COL32(64, 196, 196, 255);
    color.asset_material = IM_COL32(112, 196, 92, 255);
    color.asset_texture = IM_COL32(226, 96, 86, 255);
    color.asset_skeleton = IM_COL32(214, 206, 180, 255);
    color.asset_animation = IM_COL32(186, 132, 232, 255);
    color.asset_blend_space = IM_COL32(232, 132, 190, 255);
    color.asset_locomotion = IM_COL32(240, 150, 110, 255);
    color.asset_overlay = IM_COL32(236, 178, 84, 255);
    color.asset_scene = IM_COL32(122, 156, 240, 255);
    color.asset_terrain = IM_COL32(150, 186, 104, 255);
    color.asset_template = IM_COL32(92, 196, 214, 255);

    color.overlay = ImGui::GetColorU32(ui_hex4_over(0x303030b3, p.VIEWPORT_BACK));
    color.overlay_hover = ImGui::GetColorU32(ui_hex4_over(0x454545d0, p.VIEWPORT_BACK));
    color.overlay_active = ui_hex(p.ACCENT);
    color.overlay_label = ImGui::GetColorU32(ui_hex4_over(0x181818c0, p.VIEWPORT_BACK));

    color.gizmo_x = ui_hex(p.AXIS_X);
    color.gizmo_y = ui_hex(p.AXIS_Y);
    color.gizmo_z = ui_hex(p.AXIS_Z);
    color.gizmo_center = ui_hex(0xffffffff);
    color.gizmo_hot = ui_hex(0xffd640ff);

    color.category_header = ui_hex(p.HEADER);
    color.category_header_hover = ui_hex_over(p.PULLDOWN_HOVER, p.HEADER);
    color.category_line = ui_hex(p.BACK);
    color.axis_x = ui_hex(p.AXIS_X);
    color.axis_y = ui_hex(p.AXIS_Y);
    color.axis_z = ui_hex(p.AXIS_Z);
    color.axis_w = ui_hex(p.AXIS_W);
    color.axis_text = ui_hex(p.TEXT_DIM);

    color.shadow = IM_COL32(0, 0, 0, 80);
    color.shadow_text = IM_COL32(0, 0, 0, 140);
    color.transparent = IM_COL32(0, 0, 0, 0);

    color.viz_neutral = IM_COL32(179, 179, 179, 255);
    color.viz_barrier = IM_COL32(90, 130, 180, 220);
    color.viz_hovered = color.danger;
    color.viz_cursor = IM_COL32(255, 255, 255, 120);
    color.viz_stripe_even = ui_hex_over(p.ROW_ALTERNATE, p.BACK);
    color.viz_stripe_odd = ui_hex(p.BACK);
    color.viz_label = IM_COL32(220, 220, 220, 180);
    color.viz_ok = IM_COL32(88, 180, 120, 255);
    color.viz_warn = color.warning;
    color.viz_full = color.danger;
    color.viz_wait = IM_COL32(110, 110, 110, 90);
    color.viz_frame_marker = IM_COL32(120, 150, 255, 200);
    color.viz_submit = color.warning;

    color.grid = ui_hex(p.GRID);
    color.stats_text = ui_hex(p.STATS_TEXT);
    color.play_border = ui_hex(p.PLAY_BORDER);
    color.check = ui_hex(p.CHECK);
    color.toolbar = ui_hex(p.TOOLBAR);
    color.search = ui_hex(p.SEARCH);
    color.search_border = ui_hex(p.SEARCH_BORDER);
    color.active_icon = ui_hex(p.ACTIVE_ICON);
    color.tool_hover = ui_hex(p.TOOL_HOVER);
    color.view_header = ui_hex(p.VIEW_HEADER);
    color.view_bar = ui_hex(p.VIEW_BAR);
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
    s.ChildRounding = 0.0f;
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
