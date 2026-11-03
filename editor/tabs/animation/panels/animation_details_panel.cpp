#include <editor/tabs/animation/panels/animation_details_panel.h>
#include <core/base/strings.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/asset_registry.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>

namespace lumen {

void AnimationDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;

    const AssetRegistry::Entry* e = ctx.assets ? ctx.assets->find(tab->guid) : nullptr;
    ImGui::Text("Name: %s", e ? e->name.c_str() : "None");
    ImGui::Text("Path: %s", e ? e->asset_path.c_str() : "-");
    if (tab->disk_size) ImGui::Text("Disk Size: %s", fmt_bytes(tab->disk_size));
    if (!tab->source.empty()) {
        ImGui::TextUnformatted("Source:");
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", tab->source.c_str());
        ImGui::PopTextWrapPos();
    }

    const LAnimation& clip = tab->clip;
    const AssetRegistry::Entry* skeleton = ctx.assets ? ctx.assets->find(clip.skeleton) : nullptr;
    ImGui::Text("Skeleton: %s", skeleton ? skeleton->name.c_str() : "None");
    if (!clip.skeleton.valid()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ui_vec4(ui_style().color.warning), "No skeleton matched. Import the character, then reimport this animation.");
        ImGui::PopTextWrapPos();
    }
    if (tab->missing_tracks) ImGui::TextColored(ui_vec4(ui_style().color.warning), "%u of %u tracks aren't in the preview skeleton", tab->missing_tracks, clip.track_count());

    ImGui::Text("Duration: %.2fs", clip.duration);
    ImGui::Text("Frames: %u", clip.frame_count);
    ImGui::Text("Sample Rate: %.0f fps", clip.sample_rate);
    ImGui::Text("Tracks: %u", clip.track_count());

    if (ui_category("Playback", nullptr, true)) {
        ImGui::Checkbox("Loop", &tab->loop);
    }

    if (ui_category("Preview", nullptr, true)) {
        asset_field(ctx, "Preview Mesh", "###animation_preview_mesh", tab->preview_mesh, AssetType::MESH);
    }
}

}
