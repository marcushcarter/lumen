#pragma once
#include <imgui.h>
#include <sal.h>
#include <cstddef>
#include <cstdint>

namespace lumen {

enum class UiButton { GHOST, DANGER, PRIMARY, SUCCESS, NEUTRAL };
enum class UiRename { NONE, COMMIT, CANCEL };
enum class SplitAxis { X, Y };

struct SplitterState
{
    bool hovered = false;
    bool active = false;
    bool activated = false;
    float delta = 0.0f;
};

void ui_menu_push_style();
void ui_menu_pop_style();
void ui_context_menu_push_style();
void ui_context_menu_pop_style();
void ui_field_push_style();
void ui_field_pop_style();
void ui_push_buttons(UiButton p_kind);
void ui_pop_buttons();

bool ui_search_bar(const char* p_id, char* p_buf, size_t p_size, float p_width);
bool ui_icon_button(const char* p_icon, float p_size, const char* p_tooltip = nullptr);
bool ui_confirm_delete(const char* p_popup, bool& r_open, const char* p_message, const char* p_detail);
UiRename ui_inline_rename(ImVec2 p_pos, float p_width, char* r_buf, size_t p_size, bool& r_focus);

void ui_text_centered(ImDrawList* p_dl, ImVec2 p_min, ImVec2 p_max, ImU32 p_color, const char* p_text, const char* p_end = nullptr);
void ui_open_url(const char* p_url);
void ui_title(_Printf_format_string_ const char* p_fmt, ...);
void ui_link(const char* p_label, const char* p_url);

void ui_spacing();
void ui_section_gap();

void ui_property_row(const char* p_name, _Printf_format_string_ const char* p_fmt, ...);
void ui_property_row_value_aligned(const char* p_name, _Printf_format_string_ const char* p_fmt, ...);

void ui_cell_right(const char* p_text);
void ui_cell_right_fmt(_Printf_format_string_ const char* p_fmt, ...);

void ui_tri_right(ImU32 p_color);

ImU32 ui_category_color(const char* p_category, float p_alpha = 1.0f);
ImU32 ui_percent_color(float p_percent);

SplitterState ui_splitter(const char* p_id, SplitAxis p_axis, ImVec2 p_size, float p_grip_len = -1.0f);

bool ui_property_grid_begin(const char* p_id, float p_label_ratio = -1.0f);
void ui_property(const char* p_label);
void ui_property_grid_end();

bool ui_category(const char* p_label, bool* r_remove = nullptr);

bool ui_drag_axes(const char* p_label, ImGuiDataType p_type, void* p_data, int p_count, float p_speed, const void* p_min = nullptr, const void* p_max = nullptr, const char* p_format = nullptr, ImGuiSliderFlags p_flags = 0);
bool ui_drag_float3_axes(const char* p_label, float* r_values, float p_speed = 1.0f, const char* p_format = "%.3f");

}
