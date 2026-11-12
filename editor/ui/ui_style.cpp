#include <editor/ui/ui_style.h>
#include <core/base/color.h>
#include <imgui_internal.h>

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

static const ImFontLoader* g_base_font_loader = nullptr;
static ImFontLoader g_gamma_font_loader;
static unsigned char g_coverage_lut[256];

static bool _gamma_load_glyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void* loader_data, ImWchar codepoint, ImFontGlyph* out_glyph, float* out_advance_x)
{
    if (!g_base_font_loader->FontBakedLoadGlyph(atlas, src, baked, loader_data, codepoint, out_glyph, out_advance_x)) return false;
    if (out_glyph->PackId == ImFontAtlasRectId_Invalid || out_glyph->Colored) return true;
    const ImTextureRect* r = ImFontAtlasPackGetRect(atlas, out_glyph->PackId);
    ImTextureData* tex = atlas->TexData;
    if (!r || !tex || !tex->Pixels) return true;
    for (int y = 0; y < r->h; y++) {
        unsigned char* row = (unsigned char*)tex->GetPixelsAt(r->x, r->y + y);
        if (tex->Format == ImTextureFormat_Alpha8) {
            for (int x = 0; x < r->w; x++) row[x] = g_coverage_lut[row[x]];
        } else {
            ImU32* px = (ImU32*)(void*)row;
            for (int x = 0; x < r->w; x++) {
                const ImU32 a = g_coverage_lut[(px[x] >> IM_COL32_A_SHIFT) & 0xFF];
                px[x] = (px[x] & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
            }
        }
    }
    return true;
}

void ui_install_font_gamma(ImFontAtlas* p_atlas)
{
    g_base_font_loader = ImFontAtlasGetFontLoaderForStbTruetype();
    g_gamma_font_loader = *g_base_font_loader;
    g_gamma_font_loader.Name = "stb_truetype_gamma";
    g_gamma_font_loader.FontBakedLoadGlyph = _gamma_load_glyph;
    for (int i = 0; i < 256; i++) g_coverage_lut[i] = (unsigned char)ImClamp((int)(color_srgb_to_linear((float)i / 255.0f) * 255.0f + 0.5f), 0, 255);
    p_atlas->SetFontLoader(&g_gamma_font_loader);
}

void UiStyle::build()
{
    unit = ImFloor(20.0f * scale);
    font.size = ImFloor(11.0f * 1.21f * scale * 2.0f + 0.5f) * 0.5f;
    font.icon_size = ImFloor(font.size * 0.9f);
    space.xxs = ImFloor(2.0f * scale);
    space.xs = ImFloor(3.0f * scale);
    space.sm = ImFloor(4.0f * scale);
    space.md = ImFloor(6.0f * scale);
    space.lg = ImFloor(8.0f * scale);
    space.xl = ImFloor(10.0f * scale);
    space.xxl = ImFloor(12.0f * scale);
    space.xxxl = ImFloor(16.0f * scale);
    space.huge = ImFloor(20.0f * scale);
    radius.sm = ImFloor(0.15f * unit);
    radius.md = ImFloor(0.2f * unit);
    radius.lg = ImFloor(0.2f * unit);
    radius.xl = ImFloor(0.25f * unit);
    _build_components();
    _build_colors();
}

void UiStyle::_build_components()
{
    base.window_padding = ImVec2(space.lg, space.lg);
    base.frame_padding = ImVec2(space.md, ImFloor((unit - font.size) * 0.5f + 0.5f));
    base.item_spacing = ImVec2(space.lg, space.xxs);
    base.item_inner_spacing = ImVec2(space.sm, space.sm);
    base.cell_padding = ImVec2(space.sm, 1.0f);
    base.indent = unit;
    base.scrollbar_size = ImFloor(0.4f * unit);
    base.scrollbar_rounding = radius.pill;
    base.grab_min_size = unit;
    base.window_rounding = radius.md;
    base.frame_rounding = radius.md;
    base.popup_rounding = radius.md;
    base.grab_rounding = radius.md;
    base.tab_rounding = radius.md;
    base.window_border = stroke.hairline;
    base.popup_border = stroke.hairline;
    base.frame_border = stroke.hairline;

    dock.gap = ImFloor(3.0f * scale);
    dock.panel_padding = ImVec2(space.lg, space.sm);
    dock.drawer_padding = ImVec2(space.lg, space.lg);
    dock.drop_outline = stroke.thick;
    dock.edge_target_size = 40.0f;
    dock.edge_target_margin = space.xxxl;
    dock.edge_target_inset = space.md;
    dock.edge_target_rounding = radius.md;

    tab_strip.height_pad = unit - font.size;
    tab_strip.pad_x = space.lg;
    tab_strip.min_width = 36.0f;
    tab_strip.accent_height = 0.0f;

    dock_tab.inset_x = space.xs;
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

    menu.padding = ImVec2(space.md, space.sm);
    menu.rounding = radius.md;
    menu.border = stroke.hairline;
    menu.popup_offset = space.xxs;

    splitter.line_width = stroke.thick;
    splitter.grip_half_width = stroke.thick;
    splitter.grip_length = 40.0f;

    property.label_ratio = 0.4f;
    property.label_ratio_wide = 0.55f;
    property.row_label_width = 200.0f;
    property.section_gap = space.xxs;

    toolbar.height = ImFloor(26.0f * scale);
    toolbar.padding_x = space.lg;

    header.height = toolbar.height;
    header.pad_x = space.sm;
    header.item_gap = space.xxs;
    header.group_gap = space.lg;
    header.rounding = radius.md;
    header.menu_pad_x = space.md;
    header.dropdown_arrow = ImFloor(font.size * 0.32f);

    panel_box.margin_x = space.sm;
    panel_box.margin_top = space.sm;
    panel_box.gap = space.sm;
    panel_box.rounding = radius.md;
    panel_box.header_height = unit + ImFloor(4.0f * scale);
    panel_box.body_pad_y = space.sm;
    panel_box.grip_dot = 1.0f;
    panel_box.grip_spacing = 3.0f;

    play.margin = space.md;
    play.inset = space.xxs;
    play.rounding = radius.sm;
    play.button_extra_width = space.sm;
    play.kebab_width = 14.0f;
    play.kebab_gap = space.sm;

    titlebar.menu_padding = ImVec2(space.lg, space.xs);
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
    titlebar.logo_margin = space.md;
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

    view_menu.indicator_x = space.xxl;
    view_menu.indicator_radius = 5.0f;
    view_menu.indicator_dot_radius = 2.5f;
    view_menu.indicator_stroke = stroke.thin;
    view_menu.label_x = 28.0f;
    view_menu.icon_slot_scale = 1.4f;
    view_menu.icon_gap = space.sm;
    view_menu.arrow_inset = space.sm;

    outliner.pad_x = space.sm;
    outliner.row_pad_y = ImFloor((unit - font.size) * 0.5f);
    outliner.cell_pad_x = space.sm;
    outliner.icon_width_scale = 1.25f;
    outliner.icon_gap = space.sm;
    outliner.rename_pad_x = space.xxs;
    outliner.arrow_scale = 0.7f;
    outliner.arrow_offset_scale = 0.15f;
    outliner.padding = ImVec2(0.0f, space.lg);

    details.header_pad_y = ImFloor(2.0f * scale);
    details.header_indent = ImFloor(24.0f * scale);
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
    asset_tooltip.thumbnail_rounding = 4.0f;
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

    console_bar.pad = space.lg;
    console_bar.field_width = 360.0f;
    console_bar.field_min_width = 120.0f;
    console_bar.rounding = radius.sm;

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
    using P = Palette;

    color.editor_border = ui_hex(P::EDITOR_BORDER);
    color.topbar = ui_hex(P::TOPBAR);
    color.topbar_hover = ui_hex_over(P::PULLDOWN_HOVER, P::TOPBAR);
    color.header = ui_hex(P::HEADER);
    color.header_hover = ui_hex_over(P::PULLDOWN_HOVER, P::HEADER);
    color.outliner_back = ui_hex(P::OUTLINER_BACK);
    color.viewport_back = ui_hex(P::VIEWPORT_BACK);
    color.panel_sub = ui_hex_over(P::PANEL_SUB, P::PANEL);
    color.outline = ui_hex(P::OUTLINE);
    color.emboss = ImGui::GetColorU32(ui_hex4_over(P::EMBOSS, P::BACK));
    color.text_field = ui_hex(P::TEXT_FIELD);
    color.text_field_hover = ui_hex(P::TEXT_FIELD_HOVER);
    color.text_field_active = ui_hex(P::TEXT_FIELD_ACTIVE);
    color.menu_field = ui_hex(P::MENU);
    color.menu_field_hover = ui_hex(P::MENU_HOVER);
    color.menu_back = ui_hex(P::MENU_BACK);
    color.menu_outline = ui_hex(P::MENU_BACK_OUTLINE);
    color.menu_text = ui_hex(P::MENU_ITEM_TEXT);
    color.menu_dim = ui_hex(P::MENU_ITEM_DIM);
    color.pulldown_text = ui_hex(P::PULLDOWN_TEXT);
    color.tab = ui_hex(P::TAB);
    color.tab_hover = ui_hex(P::TAB_HOVER);
    color.tab_active = ui_hex(P::TAB_ACTIVE);
    color.list_hover = ui_hex_over(P::LIST_HOVER, P::BACK);
    color.list_selected = ui_hex(P::LIST_SELECTED);
    color.list_active = ui_hex(P::LIST_ACTIVE);
    color.row_alternate = ui_hex_over(P::ROW_ALTERNATE, P::OUTLINER_BACK);

    color.sunken = color.editor_border;
    color.chrome = color.topbar;
    color.hover = color.topbar_hover;
    color.panel = ui_hex(P::BACK);
    color.field = ui_hex(P::NUM);
    color.line = color.outline;
    color.field_hover = ui_hex(P::NUM_HOVER);
    color.field_active = ui_hex(0x6a6a6aff);
    color.field_pressed = ui_hex(P::ACCENT);

    color.text = ui_hex(P::TEXT);
    color.text_hi = ui_hex(P::TEXT_HI);
    color.text_dim = ui_hex(P::TEXT_DIM);

    const ImVec4 chrome = ui_vec4(color.chrome);
    const ImVec4 accent = ui_hex4(P::ACCENT);
    const ImVec4 fg = ui_hex4(P::TEXT);
    color.accent = ui_hex(P::ACCENT);
    color.accent_soft = _mix(chrome, accent, accent_mix.soft);
    color.accent_soft_hover = _mix(chrome, accent, accent_mix.soft_hover);
    color.accent_soft_active = _mix(chrome, accent, accent_mix.soft_active);
    color.accent_hover = ui_hex(P::ACCENT_HOVER);
    color.accent_active = _mix(accent, fg, 0.24f);
    color.selection = ui_hex(P::ACCENT_SELECT);

    color.drop_fill = ui_hex(P::ACCENT_DROP);
    color.drop_line = ui_hex(P::ACCENT);

    color.success = ui_hex(P::STATE_SUCCESS);
    color.success_soft = _mix(chrome, ui_vec4(color.success), 0.30f);
    color.success_soft_hover = _mix(chrome, ui_vec4(color.success), 0.42f);
    color.success_soft_active = _mix(chrome, ui_vec4(color.success), 0.54f);
    color.success_text = ui_hex(0x6cd27aff);
    color.warning = ui_hex(P::STATE_WARNING);
    color.danger = ui_hex(0xe63a3aff);
    color.danger_strong = ui_hex(0xc42b1cff);
    color.danger_soft = ImGui::GetColorU32(ui_hex4_over(0xe6000066, P::BACK));
    color.danger_soft_active = ImGui::GetColorU32(ui_hex4_over(0xe60000a0, P::BACK));
    color.notice = ui_hex(0xcc7529ff);
    color.favorite = ui_hex(0xffb400ff);
    color.folder = ui_hex(P::ICON_FOLDER);
    color.asset_model = IM_COL32(86, 182, 230, 255);
    color.asset_mesh = IM_COL32(64, 196, 196, 255);
    color.asset_material = IM_COL32(112, 196, 92, 255);
    color.asset_texture = IM_COL32(226, 96, 86, 255);
    color.asset_skeleton = IM_COL32(214, 206, 180, 255);
    color.asset_animation = IM_COL32(186, 132, 232, 255);
    color.asset_blend_space = IM_COL32(232, 132, 190, 255);
    color.asset_anim_graph = IM_COL32(120, 200, 220, 255);
    color.asset_overlay = IM_COL32(236, 178, 84, 255);
    color.asset_world = IM_COL32(122, 156, 240, 255);
    color.asset_terrain = IM_COL32(150, 186, 104, 255);
    color.asset_prefab = IM_COL32(92, 196, 214, 255);

    color.overlay = ImGui::GetColorU32(ui_hex4_over(0x303030b3, P::VIEWPORT_BACK));
    color.overlay_hover = ImGui::GetColorU32(ui_hex4_over(0x454545d0, P::VIEWPORT_BACK));
    color.overlay_active = ui_hex(P::ACCENT);
    color.overlay_label = ImGui::GetColorU32(ui_hex4_over(0x181818c0, P::VIEWPORT_BACK));

    color.gizmo_x = ui_hex(P::AXIS_X);
    color.gizmo_y = ui_hex(P::AXIS_Y);
    color.gizmo_z = ui_hex(P::AXIS_Z);
    color.gizmo_center = ui_hex(0xffffffff);
    color.gizmo_hot = ui_hex(0xffd640ff);

    color.category_header = ui_hex(P::PANEL);
    color.category_header_hover = ui_hex(0x444444ff);
    color.category_line = ui_hex(P::BACK);
    color.axis_x = ui_hex(P::AXIS_X);
    color.axis_y = ui_hex(P::AXIS_Y);
    color.axis_z = ui_hex(P::AXIS_Z);
    color.axis_w = ui_hex(P::AXIS_W);
    color.axis_text = ui_hex(P::TEXT_DIM);

    color.shadow = IM_COL32(0, 0, 0, 80);
    color.shadow_text = IM_COL32(0, 0, 0, 140);
    color.transparent = IM_COL32(0, 0, 0, 0);

    color.viz_neutral = IM_COL32(179, 179, 179, 255);
    color.viz_barrier = IM_COL32(90, 130, 180, 220);
    color.viz_hovered = color.danger;
    color.viz_cursor = IM_COL32(255, 255, 255, 120);
    color.viz_stripe_even = ui_hex(0x2b2b2bff);
    color.viz_stripe_odd = ui_hex(0x303030ff);
    color.viz_label = IM_COL32(220, 220, 220, 180);
    color.viz_ok = IM_COL32(88, 180, 120, 255);
    color.viz_warn = color.warning;
    color.viz_full = color.danger;
    color.viz_wait = IM_COL32(110, 110, 110, 90);
    color.viz_frame_marker = IM_COL32(120, 150, 255, 200);
    color.viz_submit = color.warning;
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
