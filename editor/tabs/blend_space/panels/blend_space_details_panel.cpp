#include <editor/tabs/blend_space/panels/blend_space_details_panel.h>
#include <editor/tabs/blend_space/blend_space_tab.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/asset_registry.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/ui_style.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace lumen {

void BlendSpaceDetailsPanel::_draw_axis(EditorContext& ctx)
{
    const LBlendSpace& space = tab->space;
    if (space.samples.empty()) {
        ImGui::TextDisabled("Add samples to blend between.");
        return;
    }
    float lo = FLT_MAX;
    float hi = -FLT_MAX;
    for (const BlendSample& s : space.samples) {
        lo = std::min(lo, s.position);
        hi = std::max(hi, s.position);
    }
    const float span = hi - lo;
    const float margin = span > 0.0f ? span * AXIS_MARGIN : 1.0f;
    lo -= margin;
    hi += margin;
    ImGui::SliderFloat(space.axis.empty() ? "Value" : space.axis.c_str(), &tab->preview_value, lo, hi, "%.2f");

    const UiStyle& ui = ui_style();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFrameHeight() * 1.4f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##axis_graph", ImVec2(std::max(w, 1.0f), h));
    if (ImGui::IsItemActive()) tab->preview_value = std::clamp(lo + (ImGui::GetIO().MousePos.x - p0.x) / std::max(w, 1.0f) * (hi - lo), lo, hi);
    auto x_of = [&](float p_v) { return p0.x + (p_v - lo) / (hi - lo) * w; };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p0.y + h * 0.5f;
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ui.color.sunken, ui.base.frame_rounding);
    dl->AddLine(ImVec2(p0.x + ui.space.sm, cy), ImVec2(p0.x + w - ui.space.sm, cy), ui.color.line, ui.stroke.thin);
    tab->weights(ctx, tab->preview_value, tab->preview_direction, tab->_weights);
    for (size_t i = 0; i < space.samples.size(); i++) {
        const float weight = i < tab->_weights.size() ? tab->_weights[i] : 0.0f;
        const float r = ui.space.sm + ui.space.xs * weight;
        const ImVec2 dot(x_of(space.samples[i].position), cy);
        dl->AddCircleFilled(dot, r, weight > 0.0f ? ui.color.asset_animation : ui.color.text_dim);
        if ((int32_t)i == selected) dl->AddCircle(dot, r + ui.space.xs, ui.color.text, 0, ui.stroke.thin);
    }
    const float mx = x_of(tab->preview_value);
    dl->AddLine(ImVec2(mx, p0.y + ui.space.xs), ImVec2(mx, p0.y + h - ui.space.xs), ui.color.accent, ui.stroke.thick);
}

void BlendSpaceDetailsPanel::_draw_pad(EditorContext& ctx)
{
    const LBlendSpace& space = tab->space;
    float max_speed = 0.0f;
    for (const BlendSample& s : space.samples) max_speed = std::max(max_speed, s.position);
    if (space.samples.empty() || max_speed <= 0.0f) {
        ImGui::TextDisabled("Add samples with a speed above 0 to blend between.");
        return;
    }
    const float limit = max_speed * (1.0f + AXIS_MARGIN);
    ImGui::SliderFloat(space.axis.empty() ? "Speed" : space.axis.c_str(), &tab->preview_value, 0.0f, limit, "%.2f");
    ImGui::SliderFloat("Direction", &tab->preview_direction, -180.0f, 180.0f, "%.0f deg");

    const UiStyle& ui = ui_style();
    const float size = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 14.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 c(p0.x + size * 0.5f, p0.y + size * 0.5f);
    const float radius = size * 0.5f - ui.space.lg;
    ImGui::InvisibleButton("##direction_pad", ImVec2(std::max(size, 1.0f), std::max(size, 1.0f)));
    if (ImGui::IsItemActive()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float dx = (m.x - c.x) / radius;
        const float dy = (c.y - m.y) / radius;
        tab->preview_value = std::clamp(std::sqrt(dx * dx + dy * dy) * limit, 0.0f, limit);
        if (dx != 0.0f || dy != 0.0f) tab->preview_direction = glm::degrees(std::atan2(dx, dy));
    }
    auto at = [&](float p_speed, float p_direction) {
        const float a = glm::radians(p_direction);
        const float r = p_speed / limit * radius;
        return ImVec2(c.x + std::sin(a) * r, c.y - std::cos(a) * r);
    };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + size, p0.y + size), ui.color.sunken, ui.base.frame_rounding);
    dl->AddCircle(c, radius, ui.color.line, 0, ui.stroke.thin);
    dl->AddLine(ImVec2(c.x, c.y - radius), ImVec2(c.x, c.y + radius), ui.color.line, ui.stroke.hairline);
    dl->AddLine(ImVec2(c.x - radius, c.y), ImVec2(c.x + radius, c.y), ui.color.line, ui.stroke.hairline);
    tab->weights(ctx, tab->preview_value, tab->preview_direction, tab->_weights);
    for (size_t i = 0; i < space.samples.size(); i++) {
        const float weight = i < tab->_weights.size() ? tab->_weights[i] : 0.0f;
        const float r = ui.space.sm + ui.space.xs * weight;
        const ImVec2 dot = at(space.samples[i].position, space.samples[i].direction);
        dl->AddCircleFilled(dot, r, weight > 0.0f ? ui.color.asset_animation : ui.color.text_dim);
        if ((int32_t)i == selected) dl->AddCircle(dot, r + ui.space.xs, ui.color.text, 0, ui.stroke.thin);
    }
    const ImVec2 marker = at(tab->preview_value, tab->preview_direction);
    dl->AddLine(c, marker, ui.color.accent, ui.stroke.thin);
    dl->AddCircle(marker, ui.space.md, ui.color.accent, 0, ui.stroke.thick);
}

void BlendSpaceDetailsPanel::_draw_samples(EditorContext& ctx)
{
    LBlendSpace& space = tab->space;
    const bool two_d = space.kind == BlendSpaceKind::TWO_D;
    const UiStyle& ui = ui_style();
    const float font = ImGui::GetFontSize();
    const float row_h = font + ui.outliner.row_pad_y * 2.0f;
    const float icon_w = font * ui.outliner.icon_width_scale;

    int32_t remove = -1;
    for (int32_t i = 0; i < (int32_t)space.samples.size(); i++) {
        const BlendSample& s = space.samples[i];
        const AssetRegistry::Entry* clip = ctx.assets ? ctx.assets->find(s.clip) : nullptr;
        ImGui::PushID(i);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##sample", selected == i, ImGuiSelectableFlags_None, ImVec2(0.0f, row_h))) selected = i;
        ui_context_menu_push_style();
        if (ImGui::BeginPopupContextItem("##sample_menu")) {
            if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Remove")) remove = i;
            ImGui::EndPopup();
        }
        ui_context_menu_pop_style();

        char value[48];
        if (two_d) std::snprintf(value, sizeof(value), "%.2f   %.0f deg", s.position, s.direction);
        else std::snprintf(value, sizeof(value), "%.2f", s.position);
        const float right = ImGui::GetItemRectMax().x - ui.outliner.cell_pad_x;
        const float text_y = p0.y + ImFloor((row_h - font) * 0.5f);
        const float value_w = ImGui::CalcTextSize(value).x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float x = p0.x + ui.outliner.cell_pad_x;
        const ImVec2 icon_size = ImGui::CalcTextSize(ICON_FA_PERSON_RUNNING);
        dl->AddText(ImVec2(ImFloor(x + (icon_w - icon_size.x) * 0.5f), text_y), ui.color.asset_animation, ICON_FA_PERSON_RUNNING);
        x += icon_w + ui.outliner.icon_gap;
        const char* name = clip ? clip->name.c_str() : "No clip";
        ImGui::RenderTextEllipsis(dl, ImVec2(x, text_y), ImVec2(right - value_w - ui.space.lg, text_y + font), right - value_w - ui.space.lg, name, nullptr, nullptr);
        dl->AddText(ImVec2(right - value_w, text_y), ui.color.text_dim, value);
        ImGui::PopID();
    }
    if (remove >= 0) {
        space.samples.erase(space.samples.begin() + remove);
        if (selected >= remove) selected--;
    }
    if (selected >= (int32_t)space.samples.size()) selected = -1;

    if (ImGui::Button(ICON_FA_PLUS " Add Sample", ImVec2(-FLT_MIN, 0.0f))) {
        const float next = space.samples.empty() ? 0.0f : space.samples.back().position + 1.0f;
        space.samples.push_back({ Guid{}, next });
        selected = (int32_t)space.samples.size() - 1;
    }
    _draw_selected(ctx);
}

void BlendSpaceDetailsPanel::_draw_selected(EditorContext& ctx)
{
    LBlendSpace& space = tab->space;
    if (selected < 0 || selected >= (int32_t)space.samples.size()) return;
    BlendSample& s = space.samples[selected];
    const bool two_d = space.kind == BlendSpaceKind::TWO_D;

    char label[32];
    std::snprintf(label, sizeof(label), "SAMPLE %d", selected + 1);
    ui_section_label(label);
    ImGui::PushID(selected);
    asset_field(ctx, "Clip", "###sample_clip", s.clip, AssetType::ANIMATION);
    if (two_d) {
        ImGui::DragFloat(space.axis.empty() ? "Speed" : space.axis.c_str(), &s.position, 0.05f, 0.0f, FLT_MAX, "%.2f");
        ImGui::DragFloat("Direction", &s.direction, 1.0f, -180.0f, 180.0f, "%.0f deg");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("0 forward, 90 right, 180 back, -90 left");
    } else {
        ImGui::DragFloat(space.axis.empty() ? "Position" : space.axis.c_str(), &s.position, 0.05f, -FLT_MAX, FLT_MAX, "%.2f");
    }
    ui_push_buttons(UiButton::DANGER);
    const bool remove = ImGui::Button(ICON_FA_TRASH_CAN "  Remove Sample");
    ui_pop_buttons();
    ImGui::PopID();
    if (remove) {
        space.samples.erase(space.samples.begin() + selected);
        selected = -1;
    }
}

void BlendSpaceDetailsPanel::_draw_overlays(EditorContext& ctx)
{
    std::vector<Guid>& overlays = tab->preview_overlays;
    int32_t remove = -1;
    for (int32_t i = 0; i < (int32_t)overlays.size(); i++) {
        char id[32];
        std::snprintf(id, sizeof(id), "###preview_overlay_%d", i);
        char label[32];
        std::snprintf(label, sizeof(label), "Overlay %d", i + 1);
        if (asset_field(ctx, label, id, overlays[i], AssetType::OVERLAY) && !overlays[i].valid()) remove = i;
    }
    if (remove >= 0) overlays.erase(overlays.begin() + remove);

    if (ImGui::Button(ICON_FA_PLUS " Add Overlay", ImVec2(-FLT_MIN, 0.0f))) ImGui::OpenPopup("##add_overlay");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Preview only, not saved with the blend space");
    Guid picked;
    if (asset_picker(ctx, "##add_overlay", picked, AssetType::OVERLAY) && picked.valid()) overlays.push_back(picked);
}

void BlendSpaceDetailsPanel::_draw_warnings(EditorContext& ctx)
{
    const LBlendSpace& space = tab->space;
    const bool two_d = space.kind == BlendSpaceKind::TWO_D;
    std::vector<std::string> lines;
    char line[160];

    Guid skeleton;
    for (size_t i = 0; i < space.samples.size(); i++) {
        const BlendSample& s = space.samples[i];
        if (!s.clip.valid()) {
            std::snprintf(line, sizeof(line), "Sample %zu has no clip", i + 1);
            lines.push_back(line);
            continue;
        }
        const BlendSpaceTab::LoadedClip* c = tab->clip(ctx, s.clip);
        if (!c) {
            std::snprintf(line, sizeof(line), "Sample %zu's clip couldn't be loaded", i + 1);
            lines.push_back(line);
            continue;
        }
        if (!skeleton.valid()) skeleton = c->clip.skeleton;
        else if (c->clip.skeleton != skeleton) {
            std::snprintf(line, sizeof(line), "Sample %zu uses a different skeleton", i + 1);
            lines.push_back(line);
        }
    }
    for (size_t i = 0; i < space.samples.size(); i++) {
        for (size_t k = 0; k < i; k++) {
            const BlendSample& a = space.samples[i];
            const BlendSample& b = space.samples[k];
            const bool same_position = std::fabs(a.position - b.position) < SAME_SAMPLE_EPSILON;
            const bool same_direction = !two_d || a.position < SAME_SAMPLE_EPSILON || std::fabs(a.direction - b.direction) < SAME_SAMPLE_EPSILON;
            if (!same_position || !same_direction) continue;
            std::snprintf(line, sizeof(line), "Samples %zu and %zu are in the same place", k + 1, i + 1);
            lines.push_back(line);
            break;
        }
    }
    if (lines.empty()) return;

    for (const std::string& l : lines) ui_warning("%s", l.c_str());
}

void BlendSpaceDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    LBlendSpace& space = tab->space;
    ui_field_push_style();

    if (ui_category("Blend Space")) {
        if (ImGui::BeginCombo("Kind", blend_space_kind_name(space.kind))) {
            for (BlendSpaceKind k : { BlendSpaceKind::ONE_D, BlendSpaceKind::TWO_D }) {
                if (ImGui::Selectable(blend_space_kind_name(k), k == space.kind)) space.kind = k;
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("1D blends along one value; 2D blends by direction and speed");
        char axis[NAME_SIZE];
        std::snprintf(axis, sizeof(axis), "%s", space.axis.c_str());
        if (ImGui::InputText("Axis", axis, sizeof(axis))) space.axis = axis;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("The value that drives this blend, like speed in m/s");
    }

    if (ui_category("Preview")) {
        asset_field(ctx, "Preview Mesh", "###blend_space_preview_mesh", space.preview_mesh, AssetType::MESH);
        if (!space.preview_mesh.valid() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Empty uses the first mesh that matches the samples' clips");
        ImGui::Checkbox("Play", &tab->playing);
        if (space.kind == BlendSpaceKind::TWO_D) _draw_pad(ctx);
        else _draw_axis(ctx);
    }

    char header[48];
    std::snprintf(header, sizeof(header), "Samples (%zu)###samples", space.samples.size());
    if (ui_category(header)) _draw_samples(ctx);
    std::snprintf(header, sizeof(header), "Preview Overlays (%zu)###preview_overlays", tab->preview_overlays.size());
    if (ui_category(header)) _draw_overlays(ctx);

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
