#include <editor/documents/animation/panels/animation_details_panel.h>
#include <core/base/strings.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/asset_registry.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace lumen {

static void _angle_text(float p_degrees, char* r_buf, size_t p_size)
{
    const float a = std::round(p_degrees);
    if (a == 0.0f) std::snprintf(r_buf, p_size, "0 deg");
    else std::snprintf(r_buf, p_size, "%.0f deg %s", std::abs(a), a < 0.0f ? "L" : "R");
}

static bool _name_field(const char* p_id, std::string& r_name)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s", r_name.c_str());
    if (!ImGui::InputText(p_id, buf, sizeof(buf))) return false;
    r_name = buf;
    return true;
}

void AnimationDetailsPanel::_draw_warnings(EditorContext& ctx)
{
    (void)ctx;
    const LAnimation& clip = tab->clip;
    if (!clip.skeleton.valid()) ui_warning("No skeleton matched. Import the character, then reimport this animation.");
    if (tab->missing_tracks) ui_warning("%u of %u tracks aren't in the preview skeleton", tab->missing_tracks, clip.track_count());
    if (clip.skeleton.valid() && clip.frame_count && clip.meta.feet[0].joint.empty() && clip.meta.feet[1].joint.empty()) ui_warning("No foot bones found. Foot bones need 'foot' or 'ankle' and a side (Left/Right, _l/_r) in their name.");
}

void AnimationDetailsPanel::_draw_events()
{
    std::vector<ClipEvent>& events = tab->events;
    const float duration = tab->clip.duration;
    int32_t remove = -1;
    for (int32_t i = 0; i < (int32_t)events.size(); ++i) {
        ClipEvent& e = events[i];
        const bool open = tab->open_event == i;
        bool removed = false;
        char summary[32];
        std::snprintf(summary, sizeof(summary), "%.2fs", e.time);
        ImGui::PushID(i);
        if (ui_list_item("##event", ICON_FA_FLAG, e.name.empty() ? "Event" : e.name.c_str(), summary, open, &removed)) tab->open_event = open ? -1 : i;
        if (removed) remove = i;
        if (open && !removed) {
            ui_list_body_begin();
            if (ui_property_grid_begin("##event_fields")) {
                ui_property("Name");
                _name_field("##name", e.name);
                ui_property("Time");
                ImGui::DragFloat("##time", &e.time, 0.005f, 0.0f, duration, "%.3f s", ImGuiSliderFlags_AlwaysClamp);
                ui_property("");
                if (ImGui::Button("Go To", ImVec2(-FLT_MIN, 0.0f))) {
                    tab->player.playing = false;
                    tab->player.time = e.time;
                }
                ui_property_grid_end();
            }
            ui_list_body_end();
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        events.erase(events.begin() + remove);
        if (tab->open_event == remove) tab->open_event = -1;
        else if (tab->open_event > remove) tab->open_event--;
    }
    if (ImGui::Button(ICON_FA_PLUS "  Add Event at Playhead", ImVec2(-FLT_MIN, 0.0f))) {
        events.push_back({ "Event", std::clamp(tab->player.time, 0.0f, duration) });
        tab->open_event = (int32_t)events.size() - 1;
    }
}

void AnimationDetailsPanel::_draw_curves()
{
    std::vector<ClipCurve>& curves = tab->curves;
    const float duration = tab->clip.duration;
    int32_t remove = -1;
    for (int32_t i = 0; i < (int32_t)curves.size(); ++i) {
        ClipCurve& c = curves[i];
        const bool open = tab->open_curve == i;
        bool removed = false;
        char summary[32];
        std::snprintf(summary, sizeof(summary), "%zu keys", c.keys.size());
        ImGui::PushID(i);
        if (ui_list_item("##curve", ICON_FA_CHART_LINE, c.name.empty() ? "Curve" : c.name.c_str(), summary, open, &removed)) tab->open_curve = open ? -1 : i;
        if (removed) remove = i;
        if (open && !removed) {
            ui_list_body_begin();
            if (ui_property_grid_begin("##curve_fields")) {
                ui_property("Name");
                _name_field("##name", c.name);
                int32_t remove_key = -1;
                for (int32_t k = 0; k < (int32_t)c.keys.size(); ++k) {
                    ImGui::PushID(k);
                    char label[16];
                    std::snprintf(label, sizeof(label), "Key %d", k + 1);
                    ui_property(label);
                    const float w = ImGui::GetContentRegionAvail().x;
                    const float button = ImGui::GetFrameHeight();
                    const float field = std::max(1.0f, (w - button - ImGui::GetStyle().ItemSpacing.x * 2.0f) * 0.5f);
                    ImGui::SetNextItemWidth(field);
                    if (ImGui::DragFloat("##t", &c.keys[k].x, 0.005f, 0.0f, duration, "%.3f s", ImGuiSliderFlags_AlwaysClamp)) {
                        std::sort(c.keys.begin(), c.keys.end(), [](const vec2& a, const vec2& b) { return a.x < b.x; });
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(field);
                    ImGui::DragFloat("##v", &c.keys[k].y, 0.01f, 0.0f, 0.0f, "%.3f");
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_XMARK, ImVec2(button, 0.0f))) remove_key = k;
                    ImGui::PopID();
                }
                if (remove_key >= 0) c.keys.erase(c.keys.begin() + remove_key);
                ui_property("");
                if (ImGui::Button("Add Key at Playhead", ImVec2(-FLT_MIN, 0.0f))) {
                    const float t = std::clamp(tab->player.time, 0.0f, duration);
                    c.keys.push_back(vec2(t, c.evaluate(t)));
                    std::sort(c.keys.begin(), c.keys.end(), [](const vec2& a, const vec2& b) { return a.x < b.x; });
                }
                ui_property("");
                if (ImGui::Button("Show in Timeline", ImVec2(-FLT_MIN, 0.0f))) {
                    tab->graph = ClipGraph::CURVE;
                    tab->graph_curve = i;
                }
                ui_property_grid_end();
            }
            ui_list_body_end();
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        curves.erase(curves.begin() + remove);
        if (tab->open_curve == remove) tab->open_curve = -1;
        else if (tab->open_curve > remove) tab->open_curve--;
        if (tab->graph_curve == remove) {
            tab->graph = ClipGraph::PHASE;
            tab->graph_curve = -1;
        } else if (tab->graph_curve > remove) {
            tab->graph_curve--;
        }
    }
    if (ImGui::Button(ICON_FA_PLUS "  Add Curve", ImVec2(-FLT_MIN, 0.0f))) {
        curves.push_back({ "Curve", { vec2(0.0f, 0.0f), vec2(duration, 0.0f) } });
        tab->open_curve = (int32_t)curves.size() - 1;
    }
}

void AnimationDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    ui_field_push_style();

    tab->draw_import();

    const LAnimation& clip = tab->clip;
    const ClipMeta& meta = clip.meta;

    if (ui_category("Info")) {
        const AssetRegistry::Entry* skeleton = ctx.assets ? ctx.assets->find(clip.skeleton) : nullptr;
        if (tab->disk_size) ImGui::Text("Disk Size: %s", fmt_bytes(tab->disk_size));
        ImGui::Text("Skeleton: %s", skeleton ? skeleton->name.c_str() : "None");
        ImGui::Text("Duration: %.2fs", clip.duration);
        ImGui::Text("Frames: %u @ %.0f fps", clip.frame_count, clip.sample_rate);
        ImGui::Text("Tracks: %u", clip.track_count());
    }

    if (ui_category("Locomotion")) {
        char angle[32];
        ImGui::Text("Role: %s", clip_role_name(meta.role));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Read from the file name, e.g. walk_start_L90. Rename and reimport to change it.");
        ImGui::Text("Gait: %s", clip_gait_name(meta.gait));
        if (meta.has_direction) {
            _angle_text(meta.direction, angle, sizeof(angle));
            ImGui::Text("Direction: %s", angle);
        } else {
            ImGui::TextDisabled("Direction: None");
        }
        ImGui::Text("Root: %s", meta.root_joint.empty() ? "None" : meta.root_joint.c_str());
        if (meta.has_root_motion()) {
            ImGui::Text("Distance: %.2f m", meta.distance);
            ImGui::Text("Speed: %.2f m/s", meta.speed);
            _angle_text(meta.move_direction, angle, sizeof(angle));
            ImGui::Text("Moves: %s", angle);
        } else {
            ImGui::TextDisabled("In place (no root motion)");
            if (meta.speed > 0.01f) ImGui::Text("Speed: %.2f m/s (from feet)", meta.speed);
        }
        _angle_text(meta.turn, angle, sizeof(angle));
        ImGui::Text("Turn: %s", angle);
        if (std::abs(meta.displacement.y) > 0.01f) ImGui::Text("Height Change: %.2f m", meta.displacement.y);
        ImGui::Text("Phase: %s", meta.has_phase() ? "Yes" : "No");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Walk cycle position. 0 = left foot down, 0.5 = right foot down.");
    }

    if (ui_category("Feet")) {
        for (int s = 0; s < (int)ClipFootSide::COUNT; ++s) {
            const ClipFoot& foot = meta.feet[s];
            const char* side = clip_foot_name((ClipFootSide)s);
            if (foot.joint.empty()) {
                ImGui::TextDisabled("%s: not found", side);
                continue;
            }
            ImGui::Text("%s: %s%s%s", side, foot.joint.c_str(), foot.toe.empty() ? "" : " + ", foot.toe.c_str());
            if (foot.contacts.empty()) {
                ImGui::TextDisabled("    No contacts");
                continue;
            }
            std::string ranges;
            for (const ClipContact& c : foot.contacts) {
                char r[32];
                std::snprintf(r, sizeof(r), "%s%u-%u", ranges.empty() ? "" : ", ", c.start + 1, c.end + 1);
                ranges += r;
            }
            ImGui::TextWrapped("    %zu contact%s: %s", foot.contacts.size(), foot.contacts.size() == 1 ? "" : "s", ranges.c_str());
        }
    }

    if (ui_category("Events")) _draw_events();
    if (ui_category("Curves")) _draw_curves();

    if (ui_category("Preview")) {
        asset_field(ctx, "Preview Mesh", "###animation_preview_mesh", tab->preview_mesh, AssetType::MESH);
        ImGui::Checkbox("Loop", &tab->loop);
    }

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
