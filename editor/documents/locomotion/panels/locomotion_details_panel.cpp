#include <editor/documents/locomotion/panels/locomotion_details_panel.h>
#include <editor/documents/locomotion/locomotion_document.h>
#include <editor/assets/asset_field.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>
#include <cfloat>
#include <cstdio>
#include <initializer_list>

namespace lumen {

void LocomotionDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    LLocomotionSet& set = tab->set;
    ui_field_push_style();

    using enum LocoSlot;
    struct Group { const char* label; std::initializer_list<LocoSlot> slots; };
    static const Group GROUPS[] = {
        { "Idle", { IDLE } },
        { "Walk", { WALK_F, WALK_B, WALK_LF, WALK_LB, WALK_RF, WALK_RB } },
        { "Run", { RUN_F, RUN_B, RUN_LF, RUN_LB, RUN_RF, RUN_RB, SPRINT_F, SPRINT_IMPULSE } },
        { "Crouch", { CROUCH_IDLE, CROUCH_ENTER, CROUCH_EXIT, CROUCH_WALK_F, CROUCH_WALK_B, CROUCH_WALK_L, CROUCH_WALK_R } },
        { "Turns", { TURN_L90, TURN_R90, TURN_L180, TURN_R180, ROTATE_L, ROTATE_R, CROUCH_TURN_L90, CROUCH_TURN_R90, CROUCH_TURN_L180, CROUCH_TURN_R180, CROUCH_ROTATE_L, CROUCH_ROTATE_R } },
        { "Air", { JUMP_WALK_LF, JUMP_WALK_RF, JUMP_RUN_LF, JUMP_RUN_RF, JUMP_LOOP, FALL_LOOP, FALL_FAST, LAND_LIGHT, LAND_HEAVY } },
        { "Actions", { ROLL, MANTLE_1M_LH, MANTLE_1M_RH, MANTLE_2M } },
    };

    if (ui_category("Clips")) {
        if (ImGui::Button(ICON_FA_WAND_MAGIC_SPARKLES "  Auto Fill Empty Slots", ImVec2(-FLT_MIN, 0.0f))) last_filled = tab->auto_fill(ctx);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Fills empty slots from animation names, e.g. Walk_F, Run_LF, Run_LB, TurnIP_R90, Rotate_L90, CLF_Walk_F.");
        if (last_filled != UINT32_MAX) ImGui::TextDisabled("Filled %u slot%s", last_filled, last_filled == 1 ? "" : "s");
        if (ImGui::Button("Clear All", ImVec2(-FLT_MIN, 0.0f))) {
            for (Guid& g : set.clips) g = Guid{};
            last_filled = UINT32_MAX;
        }
    }
    for (const Group& g : GROUPS) {
        if (!ui_category(g.label)) continue;
        for (LocoSlot slot : g.slots) {
            const uint32_t i = (uint32_t)slot;
            char id[48];
            std::snprintf(id, sizeof(id), "###loco_slot_%u", i);
            asset_field(ctx, loco_slot_label((LocoSlot)i), id, set.clips[i], AssetType::ANIMATION);
        }
    }

    if (ui_category("Settings")) {
        ImGui::DragFloat("Blend Time", &set.blend_time, 0.005f, 0.0f, 1.0f, "%.2f s", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How long a switch between idle, move and turn takes to settle");
        ImGui::DragFloat("Turn Threshold", &set.turn_threshold, 0.5f, 10.0f, 180.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("While standing, how far the look direction may get from the body before it turns in place");
        ImGui::DragFloat("Lean Angle", &set.lean_angle, 0.1f, 0.0f, 45.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("How far the spine tilts at a full lean");
        ImGui::DragFloat("Lean Acceleration", &set.lean_acceleration, 0.05f, 0.1f, 50.0f, "%.1f m/s2", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Acceleration (or turning) that gives a full lean");
    }

    ui_category_flush();
    if (!set.clip(LocoSlot::WALK_F).valid() && !set.clip(LocoSlot::RUN_F).valid() && !set.clip(LocoSlot::SPRINT_F).valid()) ui_warning("No forward cycle. The character can't play a move animation.");
    ui_field_pop_style();
}

}
