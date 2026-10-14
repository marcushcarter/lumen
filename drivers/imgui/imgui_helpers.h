#pragma once
#include <imgui.h>
#include <sal.h>
#include <cstdint>

namespace lumen {

void imgui_title(_Printf_format_string_ const char* p_fmt, ...);
void imgui_link(const char* label, const char* url);

void imgui_spacing();
void imgui_section_gap();

void imgui_property_row(const char* p_name, _Printf_format_string_ const char* p_fmt, ...);
void imgui_property_row_value_aligned(const char* p_name, _Printf_format_string_ const char* p_fmt, ...);

void imgui_cell_right(const char* p_text);
void imgui_cell_right_fmt(_Printf_format_string_ const char* p_fmt, ...);

void imgui_tri_right(ImU32 p_color);

ImU32 imgui_rg_category_u32(const char* cat, float alpha = 1.0f);
ImU32 imgui_pct_col(float pct);

enum class SplitAxis { X, Y };

struct SplitterState {
    bool hovered = false;
    bool active = false;
    bool activated = false;
    float delta = 0.0f;
};

SplitterState imgui_splitter(const char* id, SplitAxis axis, ImVec2 size, float grip_len = -1.0f);

bool imgui_property_grid_begin(const char* p_id, float p_label_ratio = -1.0f);
void imgui_property(const char* p_label);
void imgui_property_grid_end();

bool imgui_category(const char* p_label, bool* r_remove = nullptr);

bool imgui_drag_axes(const char* p_label, ImGuiDataType p_type, void* p_data, int p_count, float p_speed, const void* p_min = nullptr, const void* p_max = nullptr, const char* p_format = nullptr, ImGuiSliderFlags p_flags = 0);
bool imgui_drag_float3_axes(const char* p_label, float* r_values, float p_speed = 1.0f, const char* p_format = "%.3f");

}
