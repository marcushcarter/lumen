#include <editor/tabs/locomotion/panels/locomotion_details_panel.h>
#include <editor/tabs/locomotion/locomotion_tab.h>
#include <editor/assets/asset_field.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/ui_style.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lumen {

static void _edit_bone(const char* p_label, std::string& r_name)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s", r_name.c_str());
    if (ImGui::InputText(p_label, buf, sizeof(buf))) r_name = buf;
}

void LocomotionDetailsPanel::_draw_pad()
{
    const LLocomotion& c = tab->character;
    const float limit = std::max({ c.standing.sprint_speed, c.crouching.sprint_speed, 0.1f }) * (1.0f + PAD_MARGIN);
    const UiStyle& ui = ui_style();
    const float size = std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 14.0f);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 center(p0.x + size * 0.5f, p0.y + size * 0.5f);
    const float radius = size * 0.5f - ui.space.lg;
    ImGui::InvisibleButton("##direction_pad", ImVec2(std::max(size, 1.0f), std::max(size, 1.0f)));
    if (ImGui::IsItemActive()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float dx = (m.x - center.x) / radius;
        const float dy = (center.y - m.y) / radius;
        tab->preview_speed = std::clamp(std::sqrt(dx * dx + dy * dy) * limit, 0.0f, limit);
        if (dx != 0.0f || dy != 0.0f) tab->preview_direction = glm::degrees(std::atan2(dx, dy));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive()) ImGui::SetTooltip("Drag to set speed and direction. Up is forward.");

    const CharacterStance& stance = tab->state.stance == Stance::CROUCHING ? c.crouching : c.standing;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + size, p0.y + size), ui.color.sunken, ui.base.frame_rounding);
    dl->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), ui.color.line, ui.stroke.hairline);
    dl->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), ui.color.line, ui.stroke.hairline);
    for (float s : { stance.walk_speed, stance.run_speed, stance.sprint_speed }) dl->AddCircle(center, s / limit * radius, ui.color.line, 0, ui.stroke.thin);
    const float a = glm::radians(tab->preview_direction);
    const float r = tab->preview_speed / limit * radius;
    const ImVec2 marker(center.x + std::sin(a) * r, center.y - std::cos(a) * r);
    dl->AddLine(center, marker, ui.color.accent, ui.stroke.thin);
    dl->AddCircle(marker, ui.space.md, ui.color.accent, 0, ui.stroke.thick);
}

void LocomotionDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    LLocomotion& c = tab->character;
    ui_field_push_style();

    if (ui_category("Preview")) {
        Character& st = tab->state;
        ImGui::Checkbox("Play", &tab->playing);
        const float limit = std::max({ c.standing.sprint_speed, c.crouching.sprint_speed, 0.1f }) * (1.0f + PAD_MARGIN);
        ImGui::SliderFloat("Speed", &tab->preview_speed, 0.0f, limit, "%.2f m/s");
        ImGui::SliderFloat("Direction", &tab->preview_direction, -180.0f, 180.0f, "%.0f deg");
        _draw_pad();
        ImGui::SliderFloat("Look", &tab->preview_look, -180.0f, 180.0f, "%.0f deg");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Where the camera faces. Standing still, past 45 degrees turns in place, or past 50 rotates in place when aiming.");
        ImGui::Checkbox("Aim", &tab->preview_aim);
        ImGui::SameLine();
        bool crouch = st.stance == Stance::CROUCHING;
        if (ImGui::Checkbox("Crouch", &crouch)) st.stance = crouch ? Stance::CROUCHING : Stance::STANDING;
        ImGui::SameLine();
        bool air = st.movement == MovementState::IN_AIR;
        if (ImGui::Checkbox("In Air", &air)) {
            st.movement = air ? MovementState::IN_AIR : MovementState::GROUNDED;
            st.jumped = air;
            st.landed = !air;
            st.land_speed = tab->preview_fall_speed;
            st.has_input = tab->preview_speed > 0.01f;
        }
        ImGui::SliderFloat("Land Speed", &tab->preview_fall_speed, 0.0f, 20.0f, "%.1f m/s");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How hard the preview lands when In Air is turned off");
        const CharacterAnim& an = st.anim;
        ui_property_row("F / B / L / R", "%.2f / %.2f / %.2f / %.2f", an.velocity_blend[0], an.velocity_blend[1], an.velocity_blend[2], an.velocity_blend[3]);
        ui_property_row("Hips", "%s (%.2f)", an.hips_forward_target ? "Forward" : "Backward", an.hips_forward);
        ui_property_row("Play Rate", "%.2fx", an.play_rate);
        ui_property_row("Facing", "%.0f deg", st.yaw);
        if (an.turn_time >= 0.0f) ui_property_row("Turning", "%.0f deg clip, %.2fx", an.turn_curve.empty() ? 0.0f : an.turn_curve.back(), an.turn_scale);
        if (an.rotate_weight > 0.0f) ui_property_row("Rotating", "%.0f deg/s, %.2fx", an.rotate_speed, an.rotate_rate);
        if (an.transition_time >= 0.0f) ui_property_row("Transition", "%.2f / %.2f s", an.transition_time, an.transition_duration);
    }

    for (int s = 0; s < 2; s++) {
        CharacterStance& st = s == 0 ? c.standing : c.crouching;
        ImGui::PushID(s);
        if (ui_category(s == 0 ? "Standing" : "Crouching")) {
            asset_field(ctx, "Idle", "###stance_idle", st.idle, AssetType::ANIMATION);
            for (uint32_t d = 0; d < CYCLE_DIRECTION_COUNT; d++) {
                char id[32];
                std::snprintf(id, sizeof(id), "###stance_cycle_%u", d);
                asset_field(ctx, cycle_direction_name((CycleDirection)d), id, st.cycles[d], AssetType::BLEND_SPACE);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("1D blend spaces with walk at 1, run at 2, sprint at 3. A missing hips variant uses the other one.");
            ImGui::DragFloat("Walk Speed", &st.walk_speed, 0.01f, 0.1f, 30.0f, "%.2f m/s");
            ImGui::DragFloat("Run Speed", &st.run_speed, 0.01f, 0.1f, 30.0f, "%.2f m/s");
            ImGui::DragFloat("Sprint Speed", &st.sprint_speed, 0.01f, 0.1f, 30.0f, "%.2f m/s");
            ImGui::DragFloat("Acceleration", &st.acceleration, 0.1f, 0.1f, 200.0f, "%.1f m/s2");
            ImGui::DragFloat("Braking", &st.braking, 0.1f, 0.1f, 200.0f, "%.1f m/s2");
            ImGui::DragFloat("Rotation Rate", &st.rotation_rate, 0.1f, 0.1f, 50.0f, "%.1f");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How quickly the character turns toward where it moves. Faster when the camera turns fast.");
        }
        ImGui::PopID();
    }

    if (ui_category("In Air")) {
        asset_field(ctx, "Jump Standing", "###jump_standing", c.jump_standing, AssetType::ANIMATION);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Optional. Empty uses the walk or run jumps.");
        asset_field(ctx, "Jump Walk Left Foot", "###jump_walk_left", c.jump_walk_left, AssetType::ANIMATION);
        asset_field(ctx, "Jump Walk Right Foot", "###jump_walk_right", c.jump_walk_right, AssetType::ANIMATION);
        asset_field(ctx, "Jump Run Left Foot", "###jump_left", c.jump_left, AssetType::ANIMATION);
        asset_field(ctx, "Jump Run Right Foot", "###jump_right", c.jump_right, AssetType::ANIMATION);
        asset_field(ctx, "Jump Loop", "###jump_loop", c.jump_loop, AssetType::ANIMATION);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Optional loop while still rising after the take-off");
        ImGui::Checkbox("Flip Feet", &c.flip_feet);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Turn on if moving jumps start on the wrong foot");
        asset_field(ctx, "Fall", "###character_fall", c.fall, AssetType::ANIMATION);
        asset_field(ctx, "Fall Fast", "###fall_fast", c.fall_fast, AssetType::ANIMATION);
        ImGui::DragFloat("Fast Fall Speed", &c.fast_fall_speed, 0.1f, 0.0f, 60.0f, "%.1f m/s");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Falling faster than this switches to Fall Fast");
        asset_field(ctx, "Land Light", "###land_light", c.land_light, AssetType::ANIMATION);
        asset_field(ctx, "Land Heavy", "###land_heavy", c.land_heavy, AssetType::ANIMATION);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Stand-in for ALS's breakfall roll until actions exist");
        ImGui::DragFloat("Heavy Land Speed", &c.heavy_land_speed, 0.05f, 0.0f, 50.0f, "%.2f m/s");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Landing faster than this while moving plays the heavy land. ALS rolls here (7 m/s).");
        ImGui::DragFloat("Jump Speed", &c.jump_speed, 0.05f, 0.0f, 30.0f, "%.2f m/s");
        ImGui::DragFloat("Gravity", &c.gravity, 0.05f, 0.0f, 50.0f, "%.2f m/s2");
        ImGui::SliderFloat("Air Control", &c.air_control, 0.0f, 1.0f, "%.2f");
    }

    if (ui_category("Turns")) {
        for (int s = 0; s < 2; s++) {
            CharacterStance& st = s == 0 ? c.standing : c.crouching;
            ImGui::PushID(s);
            ui_section_label(s == 0 ? "STANDING" : "CROUCHING");
            for (uint32_t t = 0; t < TURN_CLIP_COUNT; t++) {
                char id[32];
                std::snprintf(id, sizeof(id), "###turn_%u", t);
                asset_field(ctx, turn_clip_name((TurnClip)t), id, st.turns[t], AssetType::ANIMATION);
            }
            asset_field(ctx, "Rotate Left", "###rotate_left", st.rotate_left, AssetType::ANIMATION);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Looping clip played while aiming and the camera is more than 50 degrees to the left");
            asset_field(ctx, "Rotate Right", "###rotate_right", st.rotate_right, AssetType::ANIMATION);
            ImGui::PopID();
        }
        ImGui::TextDisabled("The rotation is read from each clip.");
    }

    if (ui_category("Transitions")) {
        asset_field(ctx, "Stand to Crouch", "###stand_to_crouch", c.stand_to_crouch, AssetType::ANIMATION);
        asset_field(ctx, "Crouch to Stand", "###crouch_to_stand", c.crouch_to_stand, AssetType::ANIMATION);
        asset_field(ctx, "Stop Left Foot", "###stop_left", c.stop_left, AssetType::ANIMATION);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("ALS_N_Stop_L_Down. Played when letting go of the stick while moving, with the left foot planted.");
        asset_field(ctx, "Stop Right Foot", "###stop_right", c.stop_right, AssetType::ANIMATION);
        asset_field(ctx, "Transition L", "###transition_left", c.transition_left, AssetType::ANIMATION);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("ALS_N_Transition_L. Small re-plant step when standing with a foot out of place.");
        asset_field(ctx, "Transition R", "###transition_right", c.transition_right, AssetType::ANIMATION);
        ImGui::TextDisabled("Stance clips play when the stance changes while still. Transitions only run in game.");
    }

    if (ui_category("Lean")) {
        ImGui::DragFloat("Lean Angle", &c.lean_angle, 0.1f, 0.0f, 45.0f, "%.1f deg");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How far the spine leans into turns, speeding up and braking");
        ImGui::DragFloat("In-Air Lean Angle", &c.air_lean_angle, 0.1f, 0.0f, 45.0f, "%.1f deg");
        _edit_bone("Spine 1", c.lean_bones[0]);
        _edit_bone("Spine 2", c.lean_bones[1]);
        _edit_bone("Spine 3", c.lean_bones[2]);
        ImGui::TextDisabled("The lean is spread over these bones. Only runs in game.");
    }

    if (ui_category("Look")) {
        ImGui::Checkbox("Look At Camera", &c.look_at);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("The head follows where the camera looks. While aiming the spine turns too.");
        ImGui::DragFloat("Yaw Limit", &c.look_yaw_limit, 0.5f, 0.0f, 120.0f, "%.0f deg");
        ImGui::DragFloat("Pitch Limit", &c.look_pitch_limit, 0.5f, 0.0f, 90.0f, "%.0f deg");
        _edit_bone("Neck", c.neck_bone);
        _edit_bone("Head", c.head_bone);
    }

    if (ui_category("Foot IK")) {
        ImGui::Checkbox("Enabled", &c.foot_ik);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Plants the feet on slopes and stairs and lowers the pelvis to the lower foot. Only runs in game.");
        _edit_bone("Pelvis", c.pelvis_bone);
        _edit_bone("Thigh Left", c.thigh_bones[0]);
        _edit_bone("Calf Left", c.calf_bones[0]);
        _edit_bone("Foot Left", c.foot_bones[0]);
        _edit_bone("Thigh Right", c.thigh_bones[1]);
        _edit_bone("Calf Right", c.calf_bones[1]);
        _edit_bone("Foot Right", c.foot_bones[1]);
    }

    ui_category_flush();
    ui_field_pop_style();
}

}
