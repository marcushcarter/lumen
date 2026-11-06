#include <editor/tabs/animation/panels/animation_timeline_panel.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lumen {

void AnimationTimelinePanel::_draw_strip(ImVec2 p_min, ImVec2 p_max)
{
    LAnimation& clip = tab->clip;
    ClipPlayer& player = tab->player;
    const UiStyle& ui = ui_style();
    const float h = p_max.y - p_min.y;
    if (p_max.x - p_min.x < h * 4.0f) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool has_clip = clip.frame_count > 0;
    const uint32_t last = has_clip ? clip.frame_count - 1 : 0;
    const uint32_t frame = has_clip ? std::min((uint32_t)std::lround(player.time * clip.sample_rate), last) : 0;

    ImGui::PushID("##timeline");
    ImGui::SetCursorScreenPos(p_min);
    if (ImGui::InvisibleButton("##play", ImVec2(h, h)) && has_clip) {
        if (!player.playing && !player.loop && player.time >= clip.duration) player.time = 0.0f;
        player.playing = !player.playing;
    }
    if (ImGui::IsItemHovered()) {
        dl->AddRectFilled(p_min, ImVec2(p_min.x + h, p_max.y), ui.color.hover, ui.base.frame_rounding);
        ImGui::SetTooltip(player.playing ? "Pause" : "Play");
    }
    const char* icon = player.playing ? ICON_FA_PAUSE : ICON_FA_PLAY;
    ui_text_centered(dl, p_min, ImVec2(p_min.x + h, p_max.y), has_clip ? ui.color.text : ui.color.text_dim, icon);

    // Sized for the longest counter so the track doesn't shift while playing.
    char widest[32];
    std::snprintf(widest, sizeof(widest), "%u/%u", clip.frame_count, clip.frame_count);
    char counter[32];
    std::snprintf(counter, sizeof(counter), "%u/%u", has_clip ? frame + 1 : 0, clip.frame_count);
    const float text_w = ImGui::CalcTextSize(widest).x;
    const float text_x = p_min.x + h + ui.timeline.padding_x;
    dl->AddText(ImVec2(text_x + text_w - ImGui::CalcTextSize(counter).x, ImFloor(p_min.y + (h - ImGui::GetFontSize()) * 0.5f)), ui.color.text_dim, counter);

    const float track_min = text_x + text_w + ui.timeline.gap;
    const float track_max = p_max.x - ui.timeline.gap;
    if (track_max <= track_min) {
        ImGui::PopID();
        return;
    }
    const float cy = ImFloor((p_min.y + p_max.y) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(track_min - ui.timeline.knob_radius, p_min.y));
    ImGui::InvisibleButton("##track", ImVec2(track_max - track_min + ui.timeline.knob_radius * 2.0f, h));
    const bool active = ImGui::IsItemActive() && has_clip;
    if (active && !scrubbing) {
        scrubbing = true;
        was_playing = player.playing;
    }
    if (active) {
        player.playing = false;
        const float t = std::clamp((ImGui::GetIO().MousePos.x - track_min) / (track_max - track_min), 0.0f, 1.0f);
        player.time = t * clip.duration;
    } else if (scrubbing) {
        scrubbing = false;
        player.playing = was_playing;
    }

    const float progress = has_clip && clip.duration > 0.0f ? std::clamp(player.time / clip.duration, 0.0f, 1.0f) : 0.0f;
    const float knob_x = track_min + (track_max - track_min) * progress;
    dl->AddLine(ImVec2(track_min, cy), ImVec2(track_max, cy), ui.color.text_dim, ui.timeline.track_thickness);
    dl->AddLine(ImVec2(track_min, cy), ImVec2(knob_x, cy), ui.color.text, ui.timeline.track_thickness);
    if (has_clip) dl->AddCircleFilled(ImVec2(knob_x, cy), ui.timeline.knob_radius, ui.color.text);
    ImGui::PopID();
}

void AnimationTimelinePanel::draw_contents(EditorContext& ctx)
{
    (void)ctx;
    if (!tab) return;
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float y = ImFloor(p0.y + std::max(0.0f, (avail.y - h) * 0.5f));
    _draw_strip(ImVec2(p0.x, y), ImVec2(p0.x + avail.x, y + h));
}

}
