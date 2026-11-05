#include <editor/assets/anim_preview.h>
#include <core/animation/blend_space.h>
#include <core/animation/clip_player.h>
#include <core/animation/pose_blend.h>
#include <core/project/project.h>
#include <editor/application/editor_context.h>
#include <editor/assets/asset_registry.h>
#include <imgui.h>
#include <cmath>

namespace lumen {

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
    for (const BlendSample& s : p_space.samples) {
        if (const LoadedClip* c = clip(ctx, p_rig, s.clip)) {
            if (const Guid mesh = RigPreview::find_mesh(ctx, c->clip.skeleton); mesh.valid()) return mesh;
        }
    }
    return {};
}

void AnimPreview::blend_weights(EditorContext& ctx, const RigPreview& p_rig, const LBlendSpace& p_space, float p_value, float p_direction, std::vector<float>& r_weights)
{
    std::vector<float> positions;
    std::vector<float> directions;
    std::vector<uint32_t> usable;
    for (uint32_t i = 0; i < (uint32_t)p_space.samples.size(); i++) {
        if (!clip(ctx, p_rig, p_space.samples[i].clip)) continue;
        positions.push_back(p_space.samples[i].position);
        directions.push_back(p_space.samples[i].direction);
        usable.push_back(i);
    }
    std::vector<float> w(positions.size());
    if (p_space.kind == BlendSpaceKind::TWO_D) blend_space_polar_weights(positions.data(), directions.data(), (uint32_t)positions.size(), p_value, p_direction, w.data());
    else blend_space_1d_weights(positions.data(), (uint32_t)positions.size(), p_value, w.data());
    r_weights.assign(p_space.samples.size(), 0.0f);
    for (size_t k = 0; k < usable.size(); k++) r_weights[usable[k]] = w[k];
}

bool AnimPreview::blend_pose(EditorContext& ctx, const RigPreview& p_rig, const LBlendSpace& p_space, float p_value, float p_direction, float p_advance, float& r_phase, JointTransform* r_pose)
{
    const LSkeleton& skeleton = p_rig.skeleton_data();
    const uint32_t joints = skeleton.joint_count();
    if (joints == 0) return false;

    std::vector<float> weights;
    blend_weights(ctx, p_rig, p_space, p_value, p_direction, weights);
    _durations.assign(p_space.samples.size(), 0.0f);
    for (size_t i = 0; i < p_space.samples.size(); i++) {
        if (const LoadedClip* c = clip(ctx, p_rig, p_space.samples[i].clip)) _durations[i] = c->clip.duration;
    }
    const float cycle = blend_cycle_duration(_durations.data(), weights.data(), (uint32_t)_durations.size());
    if (cycle <= 0.0f) return false;
    r_phase += p_advance / cycle;
    r_phase -= std::floor(r_phase);

    std::vector<PoseInput> inputs;
    _sample_poses.resize(p_space.samples.size());
    for (size_t i = 0; i < p_space.samples.size(); i++) {
        if (weights[i] <= 0.0f) continue;
        const LoadedClip* c = clip(ctx, p_rig, p_space.samples[i].clip);
        if (!c) continue;
        _sample_poses[i] = skeleton.rest;
        clip_sample(c->clip, c->map, r_phase * c->clip.duration, _sample_poses[i].data());
        inputs.push_back({ _sample_poses[i].data(), weights[i] });
    }
    if (inputs.empty()) return false;
    pose_blend(inputs.data(), (uint32_t)inputs.size(), joints, r_pose);
    return true;
}

void AnimPreview::mask_weights(const LOverlay& p_overlay, const RigPreview& p_rig, std::vector<float>& r_weights) const
{
    anim_mask_weights(p_overlay.mask, p_rig.skeleton_data(), r_weights);
}

void AnimPreview::overlay_apply(EditorContext& ctx, const RigPreview& p_rig, const LOverlay& p_overlay, float p_time, float p_weight, JointTransform* r_pose)
{
    const LSkeleton& skeleton = p_rig.skeleton_data();
    const uint32_t joints = skeleton.joint_count();
    const LoadedClip* source = clip(ctx, p_rig, p_overlay.source);
    if (joints == 0 || !source || p_weight <= 0.0f) return;

    _layer = skeleton.rest;
    const float duration = source->clip.duration;
    clip_sample(source->clip, source->map, duration > 0.0f ? std::fmod(p_time, duration) : 0.0f, _layer.data());
    mask_weights(p_overlay, p_rig, _mask);

    if (p_overlay.mode == OverlayMode::OVERRIDE) {
        pose_layer(r_pose, _layer.data(), p_weight, _mask.data(), joints);
        return;
    }
    _reference = skeleton.rest;
    if (const LoadedClip* reference = clip(ctx, p_rig, p_overlay.reference)) clip_sample(reference->clip, reference->map, 0.0f, _reference.data());
    pose_additive_delta(_layer.data(), _reference.data(), joints, _layer.data());
    pose_add(r_pose, _layer.data(), p_weight, _mask.data(), joints);
}

}
