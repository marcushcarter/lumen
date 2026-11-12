#include <editor/tabs/overlay/panels/overlay_details_panel.h>
#include <editor/tabs/overlay/overlay_tab.h>
#include <core/base/strings.h>
#include <editor/assets/asset_field.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/ui_style.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cfloat>
#include <cstdio>

namespace lumen {

bool OverlayDetailsPanel::_joint_picker(const char* p_popup_id, std::string& r_name)
{
    const UiStyle& ui = ui_style();
    ImGui::SetNextWindowSizeConstraints(ImVec2(ui.asset_picker.width, 0.0f), ImVec2(ui.asset_picker.width, ui.asset_picker.max_height));
    ui_context_menu_push_style();
    const bool open = ImGui::BeginPopup(p_popup_id);
    ui_context_menu_pop_style();
    if (!open) return false;

    static char search[128] = {};
    if (ImGui::IsWindowAppearing()) {
        search[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ui_search_bar("##joint_search", search, sizeof(search), -FLT_MIN);
    const std::string needle = str_lowered(search);

    bool picked = false;
    const LSkeleton& skeleton = tab->rig_preview.skeleton_data();
    for (const std::string& name : skeleton.names) {
        if (!needle.empty() && str_lowered(name).find(needle) == std::string::npos) continue;
        if (ImGui::Selectable(name.c_str())) {
            r_name = name;
            picked = true;
        }
    }
    if (skeleton.joint_count() == 0) ImGui::TextDisabled("No skeleton to pick from");
    ImGui::EndPopup();
    return picked;
}

void OverlayDetailsPanel::_draw_joints(EditorContext& ctx, std::vector<std::string>& r_names, const char* p_add_label)
{
    (void)ctx;
    const UiStyle& ui = ui_style();
    const LSkeleton& skeleton = tab->rig_preview.skeleton_data();
    const float font = ImGui::GetFontSize();
    const float row_h = font + ui.outliner.row_pad_y * 2.0f;
    const float icon_w = font * ui.outliner.icon_width_scale;

    ImGui::PushID(p_add_label);
    int32_t remove = -1;
    for (int32_t i = 0; i < (int32_t)r_names.size(); i++) {
        ImGui::PushID(i);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::Selectable("##joint", false, ImGuiSelectableFlags_None, ImVec2(0.0f, row_h));
        ui_context_menu_push_style();
        if (ImGui::BeginPopupContextItem("##joint_menu")) {
            if (ImGui::MenuItem(ICON_FA_TRASH_CAN "  Remove")) remove = i;
            ImGui::EndPopup();
        }
        ui_context_menu_pop_style();

        const bool missing = skeleton.joint_count() > 0 && skeleton.find(r_names[i]) == SKELETON_NO_JOINT;
        const char* value = missing ? "Missing" : "";
        const float right = ImGui::GetItemRectMax().x - ui.outliner.cell_pad_x;
        const float text_y = p0.y + ImFloor((row_h - font) * 0.5f);
        const float value_w = ImGui::CalcTextSize(value).x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float x = p0.x + ui.outliner.cell_pad_x;
        const ImVec2 icon_size = ImGui::CalcTextSize(ICON_FA_BONE);
        dl->AddText(ImVec2(ImFloor(x + (icon_w - icon_size.x) * 0.5f), text_y), ui.color.asset_skeleton, ICON_FA_BONE);
        x += icon_w + ui.outliner.icon_gap;
        const float clip_x = right - value_w - ui.space.lg;
        ImGui::RenderTextEllipsis(dl, ImVec2(x, text_y), ImVec2(clip_x, text_y + font), clip_x, r_names[i].c_str(), nullptr, nullptr);
        dl->AddText(ImVec2(right - value_w, text_y), ui.color.warning, value);
        ImGui::PopID();
    }
    if (remove >= 0) r_names.erase(r_names.begin() + remove);

    char label[48];
    std::snprintf(label, sizeof(label), ICON_FA_PLUS " %s", p_add_label);
    if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f))) ImGui::OpenPopup("##add_joint");
    std::string name;
    if (_joint_picker("##add_joint", name) && std::find(r_names.begin(), r_names.end(), name) == r_names.end()) r_names.push_back(name);
    ImGui::PopID();
}

void OverlayDetailsPanel::_draw_warnings(EditorContext& ctx)
{
    const LOverlay& overlay = tab->overlay;
    const AnimPreview::LoadedClip* source = tab->anim.clip(ctx, tab->rig_preview, overlay.source);
    if (!overlay.source.valid()) ui_warning("No source animation");
    else if (!source) ui_warning("The source animation couldn't be loaded");
    else if (tab->rig_preview.skeleton.valid() && source->clip.skeleton != tab->rig_preview.skeleton) ui_warning("The source uses a different skeleton than the preview");

    if (overlay.mode == OverlayMode::ADDITIVE && !overlay.reference.valid()) ui_warning("Additive with no reference adds the offset from the rest pose");

    const LSkeleton& skeleton = tab->rig_preview.skeleton_data();
    if (skeleton.joint_count() == 0) return;
    uint32_t missing = 0;
    for (const std::string& n : overlay.mask.branches) missing += skeleton.find(n) == SKELETON_NO_JOINT;
    for (const std::string& n : overlay.mask.exclude) missing += skeleton.find(n) == SKELETON_NO_JOINT;
    if (missing) ui_warning("%u mask joints aren't in the preview skeleton", missing);
}

void OverlayDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    LOverlay& overlay = tab->overlay;
    ui_field_push_style();

    if (ui_category("Overlay")) {
        asset_field(ctx, "Source", "###overlay_source", overlay.source, AssetType::ANIMATION);
        if (ImGui::BeginCombo("Mode", overlay_mode_name(overlay.mode))) {
            for (OverlayMode m : { OverlayMode::OVERRIDE, OverlayMode::ADDITIVE }) {
                if (ImGui::Selectable(overlay_mode_name(m), m == overlay.mode)) overlay.mode = m;
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Override replaces the masked joints; additive adds on top of them");
        if (overlay.mode == OverlayMode::ADDITIVE) {
            asset_field(ctx, "Reference", "###overlay_reference", overlay.reference, AssetType::ANIMATION);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("The source adds its difference from this clip's first frame");
        }
        ImGui::DragFloat("Fade", &overlay.fade, 0.01f, 0.0f, 5.0f, "%.2f s");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Time to fade in and out in game");
        int depth = (int)overlay.mask.depth;
        if (ImGui::SliderInt("Blend Depth", &depth, 0, MAX_DEPTH)) overlay.mask.depth = (uint32_t)depth;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Joints below a branch reach full weight over this many levels");
    }

    if (ui_category("Preview")) {
        asset_field(ctx, "Preview Blend Space", "###overlay_preview_blend_space", overlay.preview_blend_space, AssetType::BLEND_SPACE);
        ImGui::Checkbox("Play", &tab->playing);
        ImGui::SliderFloat("Weight", &tab->preview_weight, 0.0f, 1.0f, "%.2f");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How far the overlay is faded in");

        const LBlendSpace* space = tab->blend_space(ctx);
        if (space && !space->samples.empty()) {
            float lo = FLT_MAX;
            float hi = -FLT_MAX;
            for (const BlendSample& s : space->samples) {
                lo = std::min(lo, s.position);
                hi = std::max(hi, s.position);
            }
            const float span = hi - lo;
            const float margin = span > 0.0f ? span * AXIS_MARGIN : 1.0f;
            const char* axis = space->axis.empty() ? "Value" : space->axis.c_str();
            ImGui::SliderFloat(axis, &tab->preview_value, 0.0f, std::max(hi, 0.0f) + margin, "%.2f");
            ImGui::SliderFloat("Direction", &tab->preview_direction, -180.0f, 180.0f, "%.0f deg");
        }
    }

    char header[48];
    std::snprintf(header, sizeof(header), "Branches (%zu)###branches", overlay.mask.branches.size());
    if (ui_category(header)) {
        if (overlay.mask.branches.empty()) ImGui::TextDisabled("No branches, the whole body is used");
        _draw_joints(ctx, overlay.mask.branches, "Add Branch");
    }
    std::snprintf(header, sizeof(header), "Exclude (%zu)###exclude", overlay.mask.exclude.size());
    if (ui_category(header)) _draw_joints(ctx, overlay.mask.exclude, "Add Exclude");

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
