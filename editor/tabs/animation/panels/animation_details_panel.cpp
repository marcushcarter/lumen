#include <editor/tabs/animation/panels/animation_details_panel.h>
#include <core/base/strings.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/asset_registry.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>

namespace lumen {

void AnimationDetailsPanel::_draw_warnings(EditorContext& ctx)
{
    (void)ctx;
    const LAnimation& clip = tab->clip;
    if (clip.skeleton.valid() && !tab->missing_tracks) return;
    if (!clip.skeleton.valid()) ui_warning("No skeleton matched. Import the character, then reimport this animation.");
    if (tab->missing_tracks) ui_warning("%u of %u tracks aren't in the preview skeleton", tab->missing_tracks, clip.track_count());
}

void AnimationDetailsPanel::draw_contents(EditorContext& ctx)
{
    if (!tab) return;
    ui_field_push_style();

    tab->draw_import();

    if (ui_category("Info")) {
        const LAnimation& clip = tab->clip;
        const AssetRegistry::Entry* skeleton = ctx.assets ? ctx.assets->find(clip.skeleton) : nullptr;
        if (tab->disk_size) ImGui::Text("Disk Size: %s", fmt_bytes(tab->disk_size));
        ImGui::Text("Skeleton: %s", skeleton ? skeleton->name.c_str() : "None");
        ImGui::Text("Duration: %.2fs", clip.duration);
        ImGui::Text("Frames: %u @ %.0f fps", clip.frame_count, clip.sample_rate);
        ImGui::Text("Tracks: %u", clip.track_count());
    }

    if (ui_category("Preview")) {
        asset_field(ctx, "Preview Mesh", "###animation_preview_mesh", tab->preview_mesh, AssetType::MESH);
        ImGui::Checkbox("Loop", &tab->loop);
    }

    ui_category_flush();
    _draw_warnings(ctx);
    ui_field_pop_style();
}

}
