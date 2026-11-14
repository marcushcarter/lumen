#include <editor/assets/anim_preview.h>
#include <core/animation/clip_player.h>
#include <core/project/project.h>
#include <editor/application/editor_context.h>
#include <editor/assets/asset_registry.h>
#include <imgui.h>

namespace lumen {

struct PreviewClips : ClipProvider
{
    AnimPreview& preview;
    EditorContext& ctx;
    const RigPreview& rig;

    PreviewClips(AnimPreview& r_preview, EditorContext& r_ctx, const RigPreview& p_rig) : preview(r_preview), ctx(r_ctx), rig(p_rig) {}

    BoundClip clip(Guid p_guid) override
    {
        const AnimPreview::LoadedClip* c = preview.clip(ctx, rig, p_guid);
        return c ? BoundClip{ &c->clip, &c->map } : BoundClip{};
    }

    const LBlendSpace* blend_space(Guid p_guid) override
    {
        return preview.blend_space(ctx, p_guid);
    }
};

template<typename T>
static const T* _load_asset(EditorContext& ctx, std::unordered_map<Guid, AnimPreview::LoadedAsset<T>>& r_cache, Guid p_guid, const char* p_kind)
{
    if (!p_guid.valid() || !ctx.assets) return nullptr;
    const AssetRegistry::Entry* entry = ctx.assets->find(p_guid);
    if (!entry) return nullptr;
    AnimPreview::LoadedAsset<T>& a = r_cache[p_guid];
    const double now = ImGui::GetTime();
    if (now - a.poll >= AnimPreview::POLL_S) {
        a.poll = now;
        std::error_code ec;
        const std::filesystem::file_time_type t = std::filesystem::last_write_time(entry->path, ec);
        if (!ec && t != a.time) {
            a.time = t;
            a.failed = a.asset.load(entry->path) != Error::OK;
            if (a.failed) log_write("Anim Preview: failed to load %s '%s'.", p_kind, entry->path.string().c_str());
        }
    }
    return a.failed || a.time == std::filesystem::file_time_type{} ? nullptr : &a.asset;
}

const AnimPreview::LoadedClip* AnimPreview::clip(EditorContext& ctx, const RigPreview& p_rig, Guid p_guid)
{
    if (!p_guid.valid() || !ctx.project) return nullptr;
    LoadedClip& c = _clips[p_guid];
    if (!c.failed && c.clip.frame_count == 0) {
        c.failed = c.clip.load_cooked(ctx.project->content_path(p_guid)) != Error::OK || c.clip.frame_count == 0;
        if (c.failed) log_write("Anim Preview: failed to load clip %s.", p_guid.to_string().c_str());
    }
    if (c.failed) return nullptr;
    if (c.mapped_version != p_rig.version) {
        c.mapped_version = p_rig.version;
        clip_track_map(p_rig.skeleton_data(), c.clip, c.map);
    }
    return &c;
}

const LBlendSpace* AnimPreview::blend_space(EditorContext& ctx, Guid p_guid)
{
    return _load_asset(ctx, _spaces, p_guid, "blend space");
}

const LOverlay* AnimPreview::overlay(EditorContext& ctx, Guid p_guid)
{
    return _load_asset(ctx, _overlays, p_guid, "overlay");
}

Guid AnimPreview::blend_space_mesh(EditorContext& ctx, const RigPreview& p_rig, const LBlendSpace& p_space)
{
    if (p_space.preview_mesh.valid()) return p_space.preview_mesh;
    for (Guid g : p_space.clip_list()) {
        if (const LoadedClip* c = clip(ctx, p_rig, g)) {
            if (const Guid mesh = RigPreview::find_mesh(ctx, c->clip.skeleton); mesh.valid()) return mesh;
        }
    }
    return {};
}

bool AnimPreview::character_pose(EditorContext& ctx, const RigPreview& p_rig, const LLocomotion& p_asset, Character& r_character, float p_dt, JointTransform* r_pose)
{
    PreviewClips clips(*this, ctx, p_rig);
    return lumen::character_pose(p_asset, r_character, clips, evaluator, p_rig.skeleton_data(), p_dt, r_pose);
}

void AnimPreview::blend_weights(EditorContext& ctx, const RigPreview& p_rig, const LBlendSpace& p_space, float p_value, std::vector<float>& r_weights)
{
    PreviewClips clips(*this, ctx, p_rig);
    evaluator.blend_weights(clips, p_space, p_value, r_weights);
}

bool AnimPreview::blend_pose(EditorContext& ctx, const RigPreview& p_rig, const LBlendSpace& p_space, float p_value, float p_dt, float& r_phase, JointTransform* r_pose)
{
    PreviewClips clips(*this, ctx, p_rig);
    return evaluator.blend_pose(clips, p_rig.skeleton_data(), p_space, p_value, 1.0f, p_dt, r_phase, r_pose);
}

void AnimPreview::mask_weights(const LOverlay& p_overlay, const RigPreview& p_rig, std::vector<float>& r_weights) const
{
    anim_mask_weights(p_overlay.mask, p_rig.skeleton_data(), r_weights);
}

void AnimPreview::overlay_apply(EditorContext& ctx, const RigPreview& p_rig, const LOverlay& p_overlay, float p_time, float p_weight, JointTransform* r_pose)
{
    mask_weights(p_overlay, p_rig, _mask);
    PreviewClips clips(*this, ctx, p_rig);
    evaluator.overlay_apply(clips, p_rig.skeleton_data(), p_overlay, _mask.data(), p_time, p_weight, r_pose);
}

}
