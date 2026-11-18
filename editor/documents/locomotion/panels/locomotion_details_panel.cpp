#include <editor/documents/locomotion/panels/locomotion_details_panel.h>
#include <editor/documents/locomotion/locomotion_document.h>
#include <editor/assets/asset_field.h>
#include <editor/ui/ui_widgets.h>
#include <editor/ui/ui_style.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdarg>
#include <cstdio>

namespace lumen {

static int g_columns = 0;
static bool g_table = false;
static int g_table_index = 0;
static bool g_section = false;

static void _grid(int p_columns)
{
    if (g_columns == p_columns) return;
    if (g_table) ImGui::EndTable();
    g_table = false;
    g_columns = p_columns;
    if (p_columns == 0) return;
    char id[24];
    std::snprintf(id, sizeof(id), "##grid_%d", g_table_index++);
    g_table = ImGui::BeginTable(id, p_columns, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH);
    if (!g_table) return;
    const ImGuiTableColumnFlags flags = ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize;
    ImGui::TableSetupColumn(nullptr, flags, 1.0f);
    ImGui::TableSetupColumn(nullptr, flags, p_columns == 2 ? 2.0f : 1.0f);
    if (p_columns == 3) ImGui::TableSetupColumn(nullptr, flags, 1.0f);
}

static void _section_end()
{
    _grid(0);
    if (!g_section) return;
    ui_list_body_end();
    g_section = false;
}

static bool _category(const char* p_label)
{
    _section_end();
    bool* open = ImGui::GetStateStorage()->GetBoolRef(ImGui::GetID(p_label), false);
    if (ui_list_item(p_label, nullptr, p_label, nullptr, *open, nullptr)) *open = !*open;
    if (!*open) return false;
    ui_list_body_begin();
    g_section = true;
    return true;
}

static bool _row(const char* p_label)
{
    _grid(2);
    if (!g_table) return false;
    ui_property(p_label);
    return true;
}

static void _info(const char* p_label, _Printf_format_string_ const char* p_fmt, ...)
{
    if (!_row(p_label)) return;
    ImGui::AlignTextToFramePadding();
    va_list args;
    va_start(args, p_fmt);
    ImGui::TextV(p_fmt, args);
    va_end(args);
}

static void _note(const char* p_text)
{
    _grid(0);
    ImGui::TextDisabled("%s", p_text);
}

static void _asset(EditorContext& ctx, const char* p_label, Guid& r_guid, AssetType p_type)
{
    if (!_row(p_label)) return;
    asset_field(ctx, nullptr, p_label, r_guid, p_type);
}

static void _pair(EditorContext& ctx, const char* p_label, Guid& r_left, Guid& r_right, AssetType p_type)
{
    _grid(3);
    if (!g_table) return;
    ui_property(p_label);
    ImGui::PushID(p_label);
    asset_field(ctx, nullptr, "###left", r_left, p_type, r_right.valid() ? "Mirrored from right" : nullptr);
    ImGui::TableSetColumnIndex(2);
    asset_field(ctx, nullptr, "###right", r_right, p_type, r_left.valid() ? "Mirrored from left" : nullptr);
    ImGui::PopID();
}

static void _bone_input(const char* p_id, std::string& r_name)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s", r_name.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputText(p_id, buf, sizeof(buf))) r_name = buf;
}

static void _bone(const char* p_label, std::string& r_name)
{
    if (!_row(p_label)) return;
    ImGui::PushID(p_label);
    _bone_input("##bone", r_name);
    ImGui::PopID();
}

static void _bone_pair(const char* p_label, std::string& r_left, std::string& r_right)
{
    _grid(3);
    if (!g_table) return;
    ui_property(p_label);
    ImGui::PushID(p_label);
    _bone_input("##left", r_left);
    ImGui::TableSetColumnIndex(2);
    _bone_input("##right", r_right);
    ImGui::PopID();
}

static void _tooltip(const char* p_text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", p_text);
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
    g_columns = 0;
    g_table = false;
    g_table_index = 0;
    g_section = false;
    const UiStyle& ui = ui_style();
    const float inset = ui_category_inset();
    {
        const ImVec2 o = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float row = ui.row;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(o, ImVec2(o.x + w, o.y + row), ui.color.header);
        const float grid_x = o.x + inset + ui.space.xl;
        const float col_w = std::max(1.0f, w - inset * 2.0f - ui.space.xl) / 3.0f;
        const float ty = std::floor(o.y + (row - ImGui::GetFontSize()) * 0.5f);
        static constexpr const char* HEADERS[3] = { "Name", "Left", "Right" };
        for (int i = 0; i < 3; i++) dl->AddText(ImVec2(std::floor(grid_x + col_w * (float)i + ImGui::GetStyle().CellPadding.x), ty), ui.color.text, HEADERS[i]);
        ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + row));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    ui_field_push_style();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(inset, ui.panel_box.gap));
    const bool shown = ImGui::BeginChild("##locomotion_scroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    if (!shown) {
        ImGui::EndChild();
        ui_field_pop_style();
        return;
    }

    if (_category("Preview")) {
        Character& st = tab->state;
        const float limit = std::max({ c.standing.sprint_speed, c.crouching.sprint_speed, 0.1f }) * (1.0f + PAD_MARGIN);
        if (_row("Play")) ImGui::Checkbox("##play", &tab->playing);
        if (_row("Speed")) ImGui::SliderFloat("##speed", &tab->preview_speed, 0.0f, limit, "%.2f m/s");
        if (_row("Direction")) ImGui::SliderFloat("##direction", &tab->preview_direction, -180.0f, 180.0f, "%.0f deg");
        _grid(0);
        _draw_pad();
        if (_row("Look")) {
            ImGui::SliderFloat("##look", &tab->preview_look, -180.0f, 180.0f, "%.0f deg");
            _tooltip("Where the camera faces. Standing still, past 45 degrees turns in place, or past 50 rotates in place when aiming.");
        }
        if (_row("Aim")) ImGui::Checkbox("##aim", &tab->preview_aim);
        if (_row("Crouch")) {
            bool crouch = st.stance == Stance::CROUCHING;
            if (ImGui::Checkbox("##crouch", &crouch)) st.stance = crouch ? Stance::CROUCHING : Stance::STANDING;
        }
        if (_row("In Air")) {
            bool air = st.movement == MovementState::IN_AIR;
            if (ImGui::Checkbox("##in_air", &air)) {
                st.movement = air ? MovementState::IN_AIR : MovementState::GROUNDED;
                st.jumped = air;
                st.landed = !air;
                st.land_speed = tab->preview_fall_speed;
                st.has_input = tab->preview_speed > 0.01f;
            }
        }
        if (_row("Land Speed")) {
            ImGui::SliderFloat("##land_speed", &tab->preview_fall_speed, 0.0f, 20.0f, "%.1f m/s");
            _tooltip("How hard the preview lands when In Air is turned off");
        }
        const CharacterAnim& an = st.anim;
        _info("F / B / L / R", "%.2f / %.2f / %.2f / %.2f", an.velocity_blend[0], an.velocity_blend[1], an.velocity_blend[2], an.velocity_blend[3]);
        _info("Hips", "%s (%.2f)", an.hips_forward_target ? "Forward" : "Backward", an.hips_forward);
        _info("Play Rate", "%.2fx", an.play_rate);
        _info("Facing", "%.0f deg", st.yaw);
        if (an.turn_time >= 0.0f) _info("Turning", "%.0f deg clip, %.2fx", an.turn_curve.empty() ? 0.0f : an.turn_curve.back(), an.turn_scale);
        if (an.rotate_weight > 0.0f) _info("Rotating", "%.0f deg/s, %.2fx", an.rotate_speed, an.rotate_rate);
        if (an.transition_time >= 0.0f) _info("Transition", "%.2f / %.2f s", an.transition_time, an.transition_duration);
    }

    for (int s = 0; s < 2; s++) {
        CharacterStance& st = s == 0 ? c.standing : c.crouching;
        _section_end();
        ImGui::PushID(s);
        if (_category(s == 0 ? "Standing" : "Crouching")) {
            _asset(ctx, "Idle", st.idle, AssetType::ANIMATION);
            _asset(ctx, "Forward", st.cycles[(uint32_t)CycleDirection::FORWARD], AssetType::BLEND_SPACE);
            _tooltip("1D blend spaces with walk at 1, run at 2, sprint at 3");
            _asset(ctx, "Backward", st.cycles[(uint32_t)CycleDirection::BACKWARD], AssetType::BLEND_SPACE);
            _pair(ctx, "Side (Hips Forward)", st.cycles[(uint32_t)CycleDirection::LEFT_FORWARD], st.cycles[(uint32_t)CycleDirection::RIGHT_FORWARD], AssetType::BLEND_SPACE);
            _pair(ctx, "Side (Hips Backward)", st.cycles[(uint32_t)CycleDirection::LEFT_BACKWARD], st.cycles[(uint32_t)CycleDirection::RIGHT_BACKWARD], AssetType::BLEND_SPACE);
            _tooltip("Optional. Empty on both sides uses the hips forward clips.");
            _pair(ctx, "Turn 90", st.turns[(uint32_t)TurnClip::LEFT_90], st.turns[(uint32_t)TurnClip::RIGHT_90], AssetType::ANIMATION);
            _tooltip("The rotation is read from each clip");
            _pair(ctx, "Turn 180", st.turns[(uint32_t)TurnClip::LEFT_180], st.turns[(uint32_t)TurnClip::RIGHT_180], AssetType::ANIMATION);
            _pair(ctx, "Rotate", st.rotate_left, st.rotate_right, AssetType::ANIMATION);
            _tooltip("Looping clip played while aiming and the camera is more than 50 degrees to the side");
            if (_row("Walk Speed")) ImGui::DragFloat("##walk_speed", &st.walk_speed, 0.01f, 0.1f, 30.0f, "%.2f m/s");
            if (_row("Run Speed")) ImGui::DragFloat("##run_speed", &st.run_speed, 0.01f, 0.1f, 30.0f, "%.2f m/s");
            if (_row("Sprint Speed")) ImGui::DragFloat("##sprint_speed", &st.sprint_speed, 0.01f, 0.1f, 30.0f, "%.2f m/s");
            if (_row("Acceleration")) ImGui::DragFloat("##acceleration", &st.acceleration, 0.1f, 0.1f, 200.0f, "%.1f m/s2");
            if (_row("Braking")) ImGui::DragFloat("##braking", &st.braking, 0.1f, 0.1f, 200.0f, "%.1f m/s2");
            if (_row("Rotation Rate")) {
                ImGui::DragFloat("##rotation_rate", &st.rotation_rate, 0.1f, 0.1f, 50.0f, "%.1f");
                _tooltip("How quickly the character turns toward where it moves. Faster when the camera turns fast.");
            }
        }
        _grid(0);
        ImGui::PopID();
    }

    if (_category("In Air")) {
        _asset(ctx, "Jump Standing", c.jump_standing, AssetType::ANIMATION);
        _tooltip("Optional. Empty uses the walk or run jumps.");
        _pair(ctx, "Jump Walk", c.jump_walk_left, c.jump_walk_right, AssetType::ANIMATION);
        _tooltip("Take-off from the left or right foot");
        _pair(ctx, "Jump Run", c.jump_left, c.jump_right, AssetType::ANIMATION);
        _asset(ctx, "Jump Loop", c.jump_loop, AssetType::ANIMATION);
        _tooltip("Optional loop while still rising after the take-off");
        if (_row("Flip Feet")) {
            ImGui::Checkbox("##flip_feet", &c.flip_feet);
            _tooltip("Turn on if moving jumps start on the wrong foot");
        }
        _asset(ctx, "Fall", c.fall, AssetType::ANIMATION);
        _asset(ctx, "Fall Fast", c.fall_fast, AssetType::ANIMATION);
        if (_row("Fast Fall Speed")) {
            ImGui::DragFloat("##fast_fall_speed", &c.fast_fall_speed, 0.1f, 0.0f, 60.0f, "%.1f m/s");
            _tooltip("Falling faster than this switches to Fall Fast");
        }
        _asset(ctx, "Land Light", c.land_light, AssetType::ANIMATION);
        _asset(ctx, "Land Heavy", c.land_heavy, AssetType::ANIMATION);
        _tooltip("Stand-in for ALS's breakfall roll until actions exist");
        if (_row("Heavy Land Speed")) {
            ImGui::DragFloat("##heavy_land_speed", &c.heavy_land_speed, 0.05f, 0.0f, 50.0f, "%.2f m/s");
            _tooltip("Landing faster than this while moving plays the heavy land. ALS rolls here (7 m/s).");
        }
        if (_row("Jump Speed")) ImGui::DragFloat("##jump_speed", &c.jump_speed, 0.05f, 0.0f, 30.0f, "%.2f m/s");
        if (_row("Air Control")) ImGui::SliderFloat("##air_control", &c.air_control, 0.0f, 1.0f, "%.2f");
    }

    if (_category("Transitions")) {
        _asset(ctx, "Stand to Crouch", c.stand_to_crouch, AssetType::ANIMATION);
        _asset(ctx, "Crouch to Stand", c.crouch_to_stand, AssetType::ANIMATION);
        _pair(ctx, "Stop", c.stop_left, c.stop_right, AssetType::ANIMATION);
        _tooltip("ALS_N_Stop_L_Down. Played when letting go of the stick while moving, with that foot planted.");
        _pair(ctx, "Step", c.transition_left, c.transition_right, AssetType::ANIMATION);
        _tooltip("ALS_N_Transition_L. Small re-plant step when standing with a foot out of place.");
        _note("Stance clips play when the stance changes while still. Steps only run in game.");
    }

    if (_category("Actions")) {
        _asset(ctx, "Roll", c.roll, AssetType::ANIMATION);
        _tooltip("Press Alt (B on a controller) to roll. The clip's root motion moves the character.");
        if (_row("Roll Land Speed")) {
            ImGui::DragFloat("##roll_land_speed", &c.roll_land_speed, 0.05f, 0.0f, 60.0f, "%.2f m/s");
            _tooltip("Landing at least this fast while moving rolls instead of a heavy landing. It also saves you from ragdolling up to 3 m/s past Ragdoll Land Speed.");
        }
        _note("Only runs in game.");
    }

    if (_category("Lean")) {
        if (_row("Lean Angle")) {
            ImGui::DragFloat("##lean_angle", &c.lean_angle, 0.1f, 0.0f, 45.0f, "%.1f deg");
            _tooltip("How far the spine leans into turns, speeding up and braking");
        }
        if (_row("In-Air Lean Angle")) ImGui::DragFloat("##air_lean_angle", &c.air_lean_angle, 0.1f, 0.0f, 45.0f, "%.1f deg");
        _bone("Spine 1", c.lean_bones[0]);
        _bone("Spine 2", c.lean_bones[1]);
        _bone("Spine 3", c.lean_bones[2]);
        _note("The lean is spread over these bones. Only runs in game.");
    }

    if (_category("Look")) {
        if (_row("Look At Camera")) {
            ImGui::Checkbox("##look_at", &c.look_at);
            _tooltip("The head follows where the camera looks. While aiming the spine turns too.");
        }
        if (_row("Yaw Limit")) ImGui::DragFloat("##look_yaw_limit", &c.look_yaw_limit, 0.5f, 0.0f, 120.0f, "%.0f deg");
        if (_row("Pitch Limit")) ImGui::DragFloat("##look_pitch_limit", &c.look_pitch_limit, 0.5f, 0.0f, 90.0f, "%.0f deg");
        _bone("Neck", c.neck_bone);
        _bone("Head", c.head_bone);
    }

    if (_category("Foot IK")) {
        if (_row("Enabled")) {
            ImGui::Checkbox("##foot_ik", &c.foot_ik);
            _tooltip("Plants the feet on slopes and stairs and lowers the pelvis to the lower foot. Only runs in game.");
        }
        _bone("Pelvis", c.pelvis_bone);
        _bone_pair("Thigh", c.thigh_bones[0], c.thigh_bones[1]);
        _bone_pair("Calf", c.calf_bones[0], c.calf_bones[1]);
        _bone_pair("Foot", c.foot_bones[0], c.foot_bones[1]);
    }

    if (_category("Ragdoll")) {
        if (_row("Enabled")) {
            ImGui::Checkbox("##ragdoll", &c.ragdoll);
            _tooltip("Builds a physics ragdoll from the skeleton. Press X in game to test it.");
        }
        if (_row("Mass")) ImGui::DragFloat("##ragdoll_mass", &c.ragdoll_mass, 0.5f, 10.0f, 500.0f, "%.1f kg");
        if (_row("Muscle Strength")) {
            ImGui::DragFloat("##muscle_strength", &c.muscle_strength, 0.01f, 0.0f, 5.0f, "%.2f");
            _tooltip("How hard the body fights while falling: braces in the air, curls up on the ground, then relaxes before getting up");
        }
        if (_row("Balance")) {
            ImGui::DragFloat("##balance", &c.balance, 0.05f, 0.0f, 30.0f, "%.2f m/s");
            _tooltip("Only attacks on the ground can knock you down, and only if they are at least this strong. Running into things on the ground never does, it makes you stumble back instead. Press H (light chest hit), J (heavy chest hit) or K (light shin hit) in game to test.");
        }
        if (_row("Head Hit Speed")) {
            ImGui::DragFloat("##head_hit_speed", &c.head_hit_speed, 0.05f, 0.0f, 30.0f, "%.2f m/s");
            _tooltip("Running into something at head height faster than this makes your head snap back and you stumble back. 0 turns it off.");
        }
        if (_row("Air Ragdoll Speed")) {
            ImGui::DragFloat("##air_ragdoll_speed", &c.air_ragdoll_speed, 0.05f, 0.0f, 30.0f, "%.2f m/s");
            _tooltip("In the air, hitting your head, a wall or an attack at least this hard ragdolls you. Softer air hits only stumble you. Legs clipping something never ragdolls.");
        }
        if (_row("Ragdoll Land Speed")) {
            ImGui::DragFloat("##ragdoll_land_speed", &c.ragdoll_land_speed, 0.05f, 0.0f, 60.0f, "%.2f m/s");
            _tooltip("Landing faster than this goes straight to ragdoll. 0 turns it off.");
        }
        _asset(ctx, "Get Up Front", c.get_up_front, AssetType::ANIMATION);
        _tooltip("Played when the ragdoll settles face down");
        _asset(ctx, "Get Up Back", c.get_up_back, AssetType::ANIMATION);
        _tooltip("Played when the ragdoll settles face up");
        _bone_pair("Upper Arm", c.upperarm_bones[0], c.upperarm_bones[1]);
        _bone_pair("Lower Arm", c.lowerarm_bones[0], c.lowerarm_bones[1]);
        _bone_pair("Hand", c.hand_bones[0], c.hand_bones[1]);
        _note("Also uses the pelvis, spine, neck, head and leg bones above. Only runs in game.");
    }

    _section_end();
    ImGui::EndChild();
    ui_field_pop_style();
}

}
