#include <editor/documents/blend_space/panels/blend_space_details_panel.h>
#include <editor/documents/blend_space/blend_space_document.h>
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
    LBlendSpace& space = tab->space;
    if (space.samples.empty()) {
        ImGui::TextDisabled("Add samples to blend between.");
        return;
    }
    float lo = std::min(space.min_position(), 0.0f);
    float hi = std::max(space.max_position(), lo + 1.0f);
    ImGui::SliderFloat("Value", &tab->preview_value, lo, hi, "%.2f");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("For characters this is the gait: walk at 1, run at 2, sprint at 3");

    const UiStyle& ui = ui_style();
    const float w = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const float h = ImGui::GetFrameHeight() * 1.4f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##axis_graph", ImVec2(w, h));
    if (ImGui::IsItemActive()) tab->preview_value = std::clamp(lo + (ImGui::GetIO().MousePos.x - p0.x) / w * (hi - lo), lo, hi);
    auto x_of = [&](float p_v) { return p0.x + (p_v - lo) / (hi - lo) * w; };

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = p0.y + h * 0.5f;
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ui.color.sunken, ui.base.frame_rounding);
    dl->AddLine(ImVec2(p0.x + ui.space.sm, cy), ImVec2(p0.x + w - ui.space.sm, cy), ui.color.line, ui.stroke.thin);
    tab->anim.blend_weights(ctx, tab->rig_preview, space, tab->preview_value, tab->weights);
    for (size_t i = 0; i < space.samples.size(); i++) {
        const float weight = i < tab->weights.size() ? tab->weights[i] : 0.0f;
        const float r = ui.space.sm + ui.space.xs * weight;
        const ImVec2 dot(x_of(space.samples[i].position), cy);
        dl->AddCircleFilled(dot, r, weight > 0.0f ? ui.color.asset_animation : ui.color.text_dim);
        if ((int32_t)i == selected) dl->AddCircle(dot, r + ui.space.xs, ui.color.text, 0, ui.stroke.thin);
    }
    const float mx = x_of(tab->preview_value);
    dl->AddLine(ImVec2(mx, p0.y + ui.space.xs), ImVec2(mx, p0.y + h - ui.space.xs), ui.color.accent, ui.stroke.thick);
}

void BlendSpaceDetailsPanel::_draw_samples(EditorContext& ctx)
{
    LBlendSpace& space = tab->space;

    int32_t remove = -1;
    for (int32_t i = 0; i < (int32_t)space.samples.size(); i++) {
        BlendSample& s = space.samples[i];
        const AssetRegistry::Entry* clip = ctx.assets ? ctx.assets->find(s.clip) : nullptr;
        const bool open = selected == i;
        bool removed = false;
        char summary[48];
        std::snprintf(summary, sizeof(summary), "pos %.2f   %.2f m/s", s.position, s.speed);
        ImGui::PushID(i);
        if (ui_list_item("##sample", ICON_FA_PERSON_RUNNING, clip ? clip->name.c_str() : "No clip", summary, open, &removed)) selected = open ? -1 : i;
        if (removed) remove = i;
        if (open && !removed) {
            ui_list_body_begin();
            if (ui_property_grid_begin("##sample_fields")) {
                ui_property("Clip");
                asset_field(ctx, "", "###sample_clip", s.clip, AssetType::ANIMATION);
                ui_property("Position");
                ImGui::DragFloat("##position", &s.position, 0.01f, -FLT_MAX, FLT_MAX, "%.2f");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Walk at 1, run at 2, sprint at 3");
                ui_property("Speed");
                ImGui::DragFloat("##speed", &s.speed, 0.01f, 0.0f, 30.0f, "%.2f m/s");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How fast the character moves in this clip. Playback is scaled to match the real speed.");
                ui_property_grid_end();
            }
            ui_list_body_end();
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        space.samples.erase(space.samples.begin() + remove);
        if (selected == remove) selected = -1;
        else if (selected > remove) selected--;
    }
    if (selected >= (int32_t)space.samples.size()) selected = -1;

    if (ImGui::Button(ICON_FA_PLUS "  Add Sample", ImVec2(-FLT_MIN, 0.0f))) {
        const float next = space.samples.empty() ? 1.0f : space.max_position() + 1.0f;
        space.samples.push_back({ Guid{}, next, 0.0f });
        selected = (int32_t)space.samples.size() - 1;
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
    char line[160];
    Guid skeleton;
    for (size_t i = 0; i < space.samples.size(); i++) {
        const BlendSample& s = space.samples[i];
        if (!s.clip.valid()) {
            std::snprintf(line, sizeof(line), "Sample %zu has no clip", i + 1);
            ui_warning("%s", line);
            continue;
        }
        const BlendSpaceDocument::LoadedClip* c = tab->clip(ctx, s.clip);
        if (!c) {
            std::snprintf(line, sizeof(line), "Sample %zu's clip couldn't be loaded", i + 1);
            ui_warning("%s", line);
            continue;
        }
        if (!skeleton.valid()) skeleton = c->clip.skeleton;
        else if (c->clip.skeleton != skeleton) {
            std::snprintf(line, sizeof(line), "Sample %zu uses a different skeleton", i + 1);
            ui_warning("%s", line);
        }
        if (s.speed <= 0.0f) {
            std::snprintf(line, sizeof(line), "Sample %zu has no speed, so playback won't match movement", i + 1);
            ui_warning("%s", line);
        }
        for (size_t k = 0; k < i; k++) {
            if (std::fabs(space.samples[k].position - s.position) >= SAME_SAMPLE_EPSILON) continue;
            std::snprintf(line, sizeof(line), "Samples %zu and %zu are in the same place", k + 1, i + 1);
            ui_warning("%s", line);
            break;
        }
    }
}

void BlendSpaceDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    LBlendSpace& space = tab->space;
    ui_field_push_style();

    char header[48];
    std::snprintf(header, sizeof(header), "Samples (%zu)###samples", space.samples.size());
    if (ui_category(header)) _draw_samples(ctx);

    if (ui_category("Preview")) {
        asset_field(ctx, "Preview Mesh", "###blend_space_preview_mesh", space.preview_mesh, AssetType::MESH);
        if (!space.preview_mesh.valid() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Empty uses the first mesh that matches the samples' clips");
        ImGui::Checkbox("Play", &tab->playing);
        _draw_axis(ctx);
    }

    std::snprintf(header, sizeof(header), "Preview Overlays (%zu)###preview_overlays", tab->preview_overlays.size());
    if (ui_category(header)) _draw_overlays(ctx);

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
