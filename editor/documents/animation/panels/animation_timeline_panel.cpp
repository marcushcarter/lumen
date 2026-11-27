#include <editor/documents/animation/panels/animation_timeline_panel.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

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
    _track_min = track_min;
    _track_max = track_max;
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

float AnimationTimelinePanel::_time_x(float p_time) const
{
    const float duration = tab->clip.duration;
    const float t = duration > 0.0f ? std::clamp(p_time / duration, 0.0f, 1.0f) : 0.0f;
    return _track_min + (_track_max - _track_min) * t;
}

void AnimationTimelinePanel::_draw_events(float p_y, float p_h)
{
    const UiStyle& ui = ui_style();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float size = std::max(3.0f, p_h * 0.22f);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    for (const ClipEvent& e : tab->events) {
        const float x = ImFloor(_time_x(e.time));
        const ImVec2 a(x, p_y + 1.0f);
        dl->AddTriangleFilled(ImVec2(a.x - size, a.y), ImVec2(a.x + size, a.y), ImVec2(a.x, a.y + size * 1.5f), ui.color.warning);
        if (ImGui::IsWindowHovered() && std::abs(mouse.x - x) <= size && mouse.y >= p_y && mouse.y <= p_y + p_h) ImGui::SetTooltip("%s  (%.2fs)", e.name.empty() ? "Event" : e.name.c_str(), e.time);
    }
}

void AnimationTimelinePanel::_draw_contacts(float p_x, float p_y, float p_h)
{
    const UiStyle& ui = ui_style();
    const LAnimation& clip = tab->clip;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float frame_w = clip.frame_count > 1 ? (_track_max - _track_min) / (float)(clip.frame_count - 1) : 0.0f;
    for (int s = 0; s < (int)ClipFootSide::COUNT; ++s) {
        const float y0 = p_y + p_h * (float)s;
        const float y1 = y0 + p_h - 1.0f;
        const ClipFoot& foot = clip.meta.feet[s];
        dl->AddText(ImVec2(p_x, ImFloor(y0 + (p_h - ImGui::GetFontSize()) * 0.5f)), foot.joint.empty() ? ui.color.text_dim : ui.color.text, s == 0 ? "Left" : "Right");
        dl->AddRectFilled(ImVec2(_track_min, y0), ImVec2(_track_max, y1), ui.color.sunken);
        for (const ClipContact& c : foot.contacts) {
            const float x0 = _time_x((float)c.start / clip.sample_rate) - frame_w * 0.5f;
            const float x1 = _time_x((float)c.end / clip.sample_rate) + frame_w * 0.5f;
            dl->AddRectFilled(ImVec2(std::max(x0, _track_min), y0 + 2.0f), ImVec2(std::min(x1, _track_max), y1 - 2.0f), ui.color.success);
        }
    }
}

void AnimationTimelinePanel::_draw_graph(float p_x, ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    const LAnimation& clip = tab->clip;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (tab->graph == ClipGraph::CURVE && (tab->graph_curve < 0 || tab->graph_curve >= (int32_t)tab->curves.size())) tab->graph = ClipGraph::PHASE;

    auto graph_label = [&](ClipGraph p_graph, int32_t p_curve) -> const char* {
        switch (p_graph) {
            case ClipGraph::PHASE: return "Phase";
            case ClipGraph::DISTANCE: return "Distance";
            case ClipGraph::REMAINING: return "Remaining";
            case ClipGraph::SPEED: return "Speed";
            case ClipGraph::CURVE: return tab->curves[p_curve].name.empty() ? "Curve" : tab->curves[p_curve].name.c_str();
        }
        return "";
    };
    ImGui::SetCursorScreenPos(ImVec2(p_x, p_min.y));
    ImGui::SetNextItemWidth(std::max(1.0f, _track_min - p_x - ui.timeline.gap));
    if (ImGui::BeginCombo("##graph", graph_label(tab->graph, tab->graph_curve))) {
        static constexpr ClipGraph BUILT_IN[] = { ClipGraph::PHASE, ClipGraph::DISTANCE, ClipGraph::REMAINING, ClipGraph::SPEED };
        for (ClipGraph g : BUILT_IN) {
            if (ImGui::Selectable(graph_label(g, -1), tab->graph == g)) tab->graph = g;
        }
        if (!tab->curves.empty()) ImGui::Separator();
        for (int32_t i = 0; i < (int32_t)tab->curves.size(); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(graph_label(ClipGraph::CURVE, i), tab->graph == ClipGraph::CURVE && tab->graph_curve == i)) {
                tab->graph = ClipGraph::CURVE;
                tab->graph_curve = i;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    dl->AddRectFilled(ImVec2(_track_min, p_min.y), ImVec2(_track_max, p_max.y), ui.color.sunken);
    if (clip.frame_count == 0 || clip.duration <= 0.0f) return;

    const ClipCurve* curve = tab->graph == ClipGraph::CURVE ? &tab->curves[tab->graph_curve] : nullptr;
    auto value = [&](float p_time) {
        switch (tab->graph) {
            case ClipGraph::PHASE: return clip.phase_at(p_time);
            case ClipGraph::DISTANCE: return clip.traveled_at(p_time);
            case ClipGraph::REMAINING: return clip.meta.distance - clip.traveled_at(p_time);
            case ClipGraph::SPEED: return tab->clip_speed_at(p_time);
            case ClipGraph::CURVE: return curve->evaluate(p_time);
        }
        return 0.0f;
    };
    const bool available = tab->graph == ClipGraph::PHASE ? clip.meta.has_phase() : tab->graph == ClipGraph::CURVE ? !curve->keys.empty() : !clip.meta.traveled.empty();
    if (!available) {
        ui_text_centered(dl, ImVec2(_track_min, p_min.y), ImVec2(_track_max, p_max.y), ui.color.text_dim, tab->graph == ClipGraph::PHASE ? "No phase (needs both feet stepping)" : "No data");
        return;
    }

    const int samples = std::max(2, (int)((_track_max - _track_min) * 0.5f));
    std::vector<float> values(samples);
    float lo = 0.0f, hi = 0.0f;
    for (int i = 0; i < samples; ++i) {
        values[i] = value(clip.duration * (float)i / (float)(samples - 1));
        lo = i == 0 ? values[i] : std::min(lo, values[i]);
        hi = i == 0 ? values[i] : std::max(hi, values[i]);
    }
    if (tab->graph == ClipGraph::PHASE) {
        lo = 0.0f;
        hi = 1.0f;
    } else if (tab->graph != ClipGraph::CURVE) {
        lo = std::min(lo, 0.0f);
    }
    if (hi - lo < 1e-4f) {
        hi += 0.5f;
        lo -= 0.5f;
    }
    const float pad = 3.0f;
    auto to_y = [&](float p_v) { return p_max.y - pad - (p_v - lo) / (hi - lo) * (p_max.y - p_min.y - pad * 2.0f); };
    for (int i = 1; i < samples; ++i) {
        if (tab->graph == ClipGraph::PHASE && std::abs(values[i] - values[i - 1]) > 0.5f) continue;
        const float x0 = _track_min + (_track_max - _track_min) * (float)(i - 1) / (float)(samples - 1);
        const float x1 = _track_min + (_track_max - _track_min) * (float)i / (float)(samples - 1);
        dl->AddLine(ImVec2(x0, to_y(values[i - 1])), ImVec2(x1, to_y(values[i])), ui.color.accent_hover, 1.5f);
    }
    if (curve) {
        for (const vec2& k : curve->keys) dl->AddCircleFilled(ImVec2(_time_x(k.x), to_y(k.y)), 3.0f, ui.color.text);
    }

    const float now = value(tab->player.time);
    char text[48];
    switch (tab->graph) {
        case ClipGraph::PHASE: std::snprintf(text, sizeof(text), "%.2f", now); break;
        case ClipGraph::DISTANCE:
        case ClipGraph::REMAINING: std::snprintf(text, sizeof(text), "%.2f m", now); break;
        case ClipGraph::SPEED: std::snprintf(text, sizeof(text), "%.2f m/s", now); break;
        case ClipGraph::CURVE: std::snprintf(text, sizeof(text), "%.3f", now); break;
    }
    dl->AddText(ImVec2(_track_min + 4.0f, p_min.y + 2.0f), ui.color.text, text);
}

void AnimationTimelinePanel::_draw_playhead(float p_y0, float p_y1)
{
    if (tab->clip.frame_count == 0) return;
    const float x = ImFloor(_time_x(tab->player.time));
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x, p_y0), ImVec2(x, p_y1), ui_style().color.text, 1.0f);
}

void AnimationTimelinePanel::draw_contents(EditorContext& ctx)
{
    (void)ctx;
    if (!tab) return;
    const UiStyle& ui = ui_style();
    const float h = ImGui::GetFrameHeight();
    const float lane = ImFloor(h * 0.75f);
    const float gap = ImGui::GetStyle().ItemSpacing.y;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float x1 = p0.x + avail.x;

    _track_min = _track_max = 0.0f;
    _draw_strip(ImVec2(p0.x, p0.y), ImVec2(x1, p0.y + h));
    if (_track_max <= _track_min) return;
    _draw_events(p0.y, h);

    const float contacts_y = p0.y + h + gap;
    const float graph_y = contacts_y + lane * 2.0f + gap;
    const float graph_bottom = std::max(graph_y + h * 2.0f, p0.y + avail.y);
    const float label_x = p0.x + ui.timeline.padding_x;
    _draw_contacts(label_x, contacts_y, lane);
    _draw_graph(label_x, ImVec2(_track_min, graph_y), ImVec2(_track_max, graph_bottom));
    _draw_playhead(contacts_y, graph_bottom);
    ImGui::SetCursorScreenPos(ImVec2(p0.x, graph_bottom));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

}
