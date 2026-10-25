#pragma once
#include <imgui.h>

namespace lumen {

struct UiStyle
{
    /*****************/
    /**** TOKENS *****/
    /*****************/

    struct Typography {
        float size = 12.0f;
        float icon_offset_y = 1.0f;
    };

    struct Space {
        float xxs = 2.0f;
        float xs = 3.0f;
        float sm = 4.0f;
        float md = 6.0f;
        float lg = 8.0f;
        float xl = 10.0f;
        float xxl = 12.0f;
        float xxxl = 16.0f;
        float huge = 20.0f;
    };

    struct Radius {
        float none = 0.0f;
        float sm = 3.0f;
        float md = 4.0f;
        float lg = 5.0f;
        float xl = 6.0f;
        float pill = 999.0f;
    };

    struct Stroke {
        float hairline = 1.0f;
        float thin = 1.5f;
        float thick = 2.0f;
    };

    struct SurfaceMix {
        float sunken = 0.0f;
        float chrome = 0.045f;
        float hover = 0.06f;
        float panel = 0.08f;
        float field = 0.11f;
        float line = 0.12f;
        float field_hover = 0.14f;
        float field_active = 0.17f;
        float field_pressed = 0.20f;
    };

    struct AccentMix {
        float soft = 0.18f;
        float soft_hover = 0.30f;
        float soft_active = 0.42f;
    };

    struct Alpha {
        float disabled = 0.376f;
        float overlay_active = 0.51f;
        float faint = 0.2f;
        float selection = 0.2f;
        float category_fill = 0.45f;
    };

    /*****************/
    /**** COLORS *****/
    /*****************/

    struct Colors {
        ImU32 sunken;
        ImU32 chrome;
        ImU32 hover;
        ImU32 panel;
        ImU32 field;
        ImU32 line;
        ImU32 field_hover;
        ImU32 field_active;
        ImU32 field_pressed;

        ImU32 text;
        ImU32 text_dim;

        ImU32 accent;
        ImU32 accent_soft;
        ImU32 accent_soft_hover;
        ImU32 accent_soft_active;
        ImU32 accent_hover;
        ImU32 accent_active;
        ImU32 selection;

        ImU32 drop_fill;
        ImU32 drop_line;

        ImU32 success;
        ImU32 success_soft;
        ImU32 success_soft_hover;
        ImU32 success_soft_active;
        ImU32 success_text;
        ImU32 warning;
        ImU32 danger;
        ImU32 danger_strong;
        ImU32 danger_soft;
        ImU32 danger_soft_active;
        ImU32 notice;
        ImU32 favorite;
        ImU32 folder;

        ImU32 overlay;
        ImU32 overlay_hover;
        ImU32 overlay_active;
        ImU32 overlay_label;

        ImU32 gizmo_x;
        ImU32 gizmo_y;
        ImU32 gizmo_z;
        ImU32 gizmo_center;
        ImU32 gizmo_hot;

        ImU32 category_header;
        ImU32 category_header_hover;
        ImU32 category_line;
        ImU32 axis_x;
        ImU32 axis_y;
        ImU32 axis_z;
        ImU32 axis_w;
        ImU32 axis_text;

        ImU32 shadow;
        ImU32 shadow_text;
        ImU32 transparent;

        ImU32 viz_neutral;
        ImU32 viz_barrier;
        ImU32 viz_hovered;
        ImU32 viz_cursor;
        ImU32 viz_stripe_even;
        ImU32 viz_stripe_odd;
        ImU32 viz_label;
        ImU32 viz_ok;
        ImU32 viz_warn;
        ImU32 viz_full;
        ImU32 viz_wait;
        ImU32 viz_frame_marker;
        ImU32 viz_submit;
    };

    /*****************/
    /** COMPONENTS ***/
    /*****************/

    struct Base {
        ImVec2 window_padding;
        ImVec2 frame_padding;
        ImVec2 item_spacing;
        ImVec2 item_inner_spacing;
        ImVec2 cell_padding;
        float indent;
        float scrollbar_size;
        float scrollbar_rounding;
        float grab_min_size;
        float window_rounding;
        float frame_rounding;
        float popup_rounding;
        float grab_rounding;
        float tab_rounding;
        float window_border;
        float popup_border;
        float frame_border;
    };

    struct Dock {
        float gap;
        ImVec2 panel_padding;
        float panel_scrollbar;
        ImVec2 drawer_padding;
        float drop_outline;
        float edge_target_size;
        float edge_target_margin;
        float edge_target_inset;
        float edge_target_rounding;
    };

    struct TabStrip {
        float height_pad;
        float pad_x;
        float min_width;
        float accent_height;
    };

    struct DockTab {
        float inset_x;
        float inset_top;
        float gap;
        float rounding;
        float close_pad;
        float close_size_pad;
        float close_rounding;
        float close_glyph_scale;
        float close_glyph_stroke;
        float label_min_pad;
        const char* label_trail;
    };

    struct Menu {
        ImVec2 padding;
        float rounding;
        float border;
        float popup_offset;
    };

    struct Splitter {
        float line_width;
        float grip_half_width;
        float grip_length;
    };

    struct Property {
        float label_ratio;
        float label_ratio_wide;
        float row_label_width;
        float section_gap;
    };

    struct Toolbar {
        float height;
        float padding_x;
    };

    struct PlayControls {
        float margin;
        float inset;
        float rounding;
        float button_extra_width;
        float kebab_width;
        float kebab_gap;
    };

    struct Titlebar {
        ImVec2 menu_padding;
        float tab_height;
        float tab_pad_x;
        float tab_rounding;
        float tab_inner_spacing;
        float caption_width;
        float caption_glyph;
        float caption_glyph_offset;
        float caption_stroke;
        float caption_close_stroke;
        float logo_margin;
        float logo_rounding;
        float menu_offset_x;
        float title_margin;
        float stats_margin;
    };

    struct Overlay {
        float margin;
        float spacing;
        float group_gap;
        float rounding;
    };

    struct ViewMenuRow {
        float indicator_x;
        float indicator_radius;
        float indicator_dot_radius;
        float indicator_stroke;
        float label_x;
        float icon_slot_scale;
        float icon_gap;
        float arrow_inset;
    };

    struct Outliner {
        float pad_x;
        float row_pad_y;
        float cell_pad_x;
        float icon_width_scale;
        float icon_gap;
        float rename_pad_x;
        float arrow_scale;
        float arrow_offset_scale;
        ImVec2 padding;
        float label_column_weight;
        float type_column_weight;
    };

    struct Details {
        float header_pad_y;
        float header_indent;
        float button_inset;
        float arrow_x;
        float arrow_scale;
        float line;
        float axis_tab_width;
        float axis_gap;
    };

    struct ListRow {
        float height;
        float spacing;
        float rounding;
        float pad;
        float thumb_rounding;
        float star_stroke;
        float subtitle_scale;
        float subtitle_gap;
    };

    struct ProjectManager {
        float inspector_width;
        float filter_width;
        float sort_width;
        float chip_pad_x;
        float row_height;
        float row_pad_x;
        float row_gap;
        float row_accent_bar;
        float thumb_size;
        float thumb_rounding;
        float hero_height;
        ImVec2 inspector_padding;
        float title_font_size;
        float action_height;
        float action_gap;
        float modal_width;
        float browse_width;
        float button_width;
    };

    struct AssetBrowser {
        float row_pad_y;
        float chevron_width;
        float icon_gap;
        float crumb_gap;
        float item_icon;
        float item_pad;
        float item_text_top;
        float item_subtitle_gap;
        float progress_height;
        ImVec2 grid_spacing;
        float splitter;
        float min_side;
        float divider_shadow;
    };

    struct Profiler {
        float lane_gap;
        float section_height;
        float pass_height;
        float barrier_height;
        float draw_height;
        float bar_inset;
        float bar_rounding;
        float label_pad;
        float label_min_width;
        float ruler_text_pad;
        ImVec2 status_pad;
        float cursor_stroke;
        float side_panel_ratio;
        float plot_aspect;
        float lane_header_height;
        float zone_height;
        float section_gap;
        float marker_stroke;
    };

    struct DataTable {
        float column_xs;
        float column_sm;
        float column_md;
        float column_lg;
        float button_width;
        float bar_rounding;
        float block_gap;
    };

    struct ConsoleBar {
        float pad;
        float field_width;
        float field_min_width;
        float rounding;
    };

    struct Gizmo {
        float arrow_ratio;
        float ring_scale;
        float scale_handle_scale;
        float axis_thickness;
        float ring_thickness;
        float head_length;
        float head_width;
        float handle_size;
        float center_radius;
        float label_offset;
        float hit_tolerance;
        float drag_alpha;
    };

    Typography font;
    Space space;
    Radius radius;
    Stroke stroke;
    SurfaceMix surface_mix;
    AccentMix accent_mix;
    Alpha alpha;

    Colors color;

    Base base;
    Dock dock;
    TabStrip tab_strip;
    DockTab dock_tab;
    Menu menu;
    Splitter splitter;
    Property property;
    Toolbar toolbar;
    PlayControls play;
    Titlebar titlebar;
    Overlay overlay;
    ViewMenuRow view_menu;
    Outliner outliner;
    Details details;
    ListRow list_row;
    ProjectManager project_manager;
    AssetBrowser asset_browser;
    Profiler profiler;
    DataTable data_table;
    ConsoleBar console_bar;
    Gizmo gizmo;

    void build();
    void apply_to_imgui() const;

    void _build_components();
    void _build_colors();
};

UiStyle& ui_style();

ImU32 ui_with_alpha(ImU32 p_color, float p_alpha);
ImVec4 ui_vec4(ImU32 p_color);

}