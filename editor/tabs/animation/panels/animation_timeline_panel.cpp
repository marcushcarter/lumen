#include <editor/tabs/animation/panels/animation_timeline_panel.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace lumen {

void AnimationTimelinePanel::draw_contents(EditorContext& ctx)
{
    (void)ctx;
    if (!tab) return;
    const LAnimation& clip = tab->clip;
    ClipPlayer& player = tab->player;
    if (clip.frame_count == 0) {
        ImGui::TextDisabled("No animation data");
        return;
    }

    const uint32_t last = clip.frame_count - 1;
    const uint32_t frame = std::min((uint32_t)std::lround(player.time * clip.sample_rate), last);
    auto go_to = [&](uint32_t p_frame) {
        player.playing = false;
        player.time = std::min((float)p_frame / clip.sample_rate, clip.duration);
    };

    if (ImGui::Button(ICON_FA_BACKWARD_STEP)) go_to(frame > 0 ? frame - 1 : 0);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Previous frame");
    ImGui::SameLine();
    if (ImGui::Button(player.playing ? ICON_FA_PAUSE "###play" : ICON_FA_PLAY "###play")) {
        if (!player.playing && !player.loop && player.time >= clip.duration) player.time = 0.0f;
        player.playing = !player.playing;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(player.playing ? "Pause" : "Play");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FORWARD_STEP)) go_to(std::min(frame + 1, last));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Next frame");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(SPEED_WIDTH);
    ImGui::DragFloat("##speed", &player.speed, 0.01f, 0.0f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Playback speed");

    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Frame %u / %u    %.2fs / %.2fs", frame, last, player.time, clip.duration);

    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##time", &player.time, 0.0f, clip.duration, "", ImGuiSliderFlags_AlwaysClamp)) player.playing = false;
}

}
