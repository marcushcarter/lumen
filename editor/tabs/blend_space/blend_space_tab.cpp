#include <editor/tabs/blend_space/blend_space_tab.h>
#include <core/animation/blend_space.h>
#include <core/animation/clip_player.h>
#include <core/animation/pose_blend.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <editor/tabs/blend_space/panels/blend_space_details_panel.h>
#include <imgui.h>
#include <cmath>

namespace lumen {

Error BlendSpaceTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    if (space.load(path) != OK || space.guid != guid) {
        log_write("Blend Space Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = space;
    _refresh_title();

    _create_layout(ctx, "BlendSpaceHost");
    layout.add_panel<BlendSpaceDetailsPanel>()->tab = this;
    layout.viewport.overlay = [this](ImDrawList* p_draw_list, ImVec2 p_pos, ImVec2 p_size) { rig_preview.draw_bones(p_draw_list, p_pos, p_size, orbit.camera); };
    layout.viewport.overlay_controls = [this](OverlayBar& r_bar) { rig_preview.draw_controls(r_bar); };

    _create_preview();
    preview.remove<StaticTag>(_preview_entity);
    preview.add<PoseComponent>(_preview_entity);

    orbit.update();
    return OK;
}

Error BlendSpaceTab::save(EditorContext& ctx)
{
    (void)ctx;
    if (space.save(path) != Error::OK) {
        log_write("Blend Space Tab: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = space;
    _refresh_title();
    return Error::OK;
}

void BlendSpaceTab::on_update(EditorContext& ctx, float p_dt)
{
    _draw_layout(ctx);
    _update_orbit();
    _sync_preview(ctx);
    _pose_preview(ctx, p_dt);
    rig_preview.apply(preview, _preview_entity);

    vec3 center;
    float radius;
    if (!_framed && ctx.renderer && rig_preview.bounds(ctx.renderer->geometry, _shown_mesh, center, radius)) {
        orbit.frame(center, radius);
        _framed = true;
    }
    orbit.update();
}

const BlendSpaceTab::LoadedClip* BlendSpaceTab::clip(EditorContext& ctx, Guid p_guid)
{
    if (!p_guid.valid() || !ctx.project) return nullptr;
    LoadedClip& c = _clips[p_guid];
    if (!c.failed && c.clip.frame_count == 0) {
        c.failed = c.clip.load_cooked(ctx.project->content_path(p_guid)) != Error::OK || c.clip.frame_count == 0;
        if (c.failed) log_write("Blend Space Tab: failed to load clip %s.", p_guid.to_string().c_str());
    }
    if (c.failed) return nullptr;
    if (c.mapped_version != rig_preview.version) {
        c.mapped_version = rig_preview.version;
        clip_track_map(rig_preview.skeleton_data(), c.clip, c.map);
    }
    return &c;
}

Guid BlendSpaceTab::preview_mesh(EditorContext& ctx)
{
    if (space.preview_mesh.valid()) return space.preview_mesh;
    for (const BlendSample& s : space.samples) {
        if (const LoadedClip* c = clip(ctx, s.clip)) {
            if (const Guid mesh = RigPreview::find_mesh(ctx, c->clip.skeleton); mesh.valid()) return mesh;
        }
    }
    return {};
}

void BlendSpaceTab::weights(EditorContext& ctx, float p_value, float p_direction, std::vector<float>& r_weights)
{
    std::vector<float> positions;
    std::vector<float> directions;
    std::vector<uint32_t> usable;
    for (uint32_t i = 0; i < (uint32_t)space.samples.size(); i++) {
        if (!clip(ctx, space.samples[i].clip)) continue;
        positions.push_back(space.samples[i].position);
        directions.push_back(space.samples[i].direction);
        usable.push_back(i);
    }
    std::vector<float> w(positions.size());
    if (space.kind == BlendSpaceKind::TWO_D) blend_space_polar_weights(positions.data(), directions.data(), (uint32_t)positions.size(), p_value, p_direction, w.data());
    else blend_space_1d_weights(positions.data(), (uint32_t)positions.size(), p_value, w.data());
    r_weights.assign(space.samples.size(), 0.0f);
    for (size_t k = 0; k < usable.size(); k++) r_weights[usable[k]] = w[k];
}

void BlendSpaceTab::_sync_preview(EditorContext& ctx)
{
    const Guid mesh = preview_mesh(ctx);
    if (mesh != _shown_mesh && ctx.renderer) {
        _shown_mesh = mesh;
        ctx.renderer->models.set(_preview_model, mesh, {});
        _framed = false;
    }
    rig_preview.refresh(ctx, _shown_mesh, false);
}

void BlendSpaceTab::_pose_preview(EditorContext& ctx, float p_dt)
{
    const LSkeleton& skeleton = rig_preview.skeleton_data();
    const uint32_t joints = skeleton.joint_count();
    if (joints == 0) return;

    weights(ctx, preview_value, preview_direction, _weights);
    std::vector<float> durations(space.samples.size(), 0.0f);
    for (size_t i = 0; i < space.samples.size(); i++) {
        if (const LoadedClip* c = clip(ctx, space.samples[i].clip)) durations[i] = c->clip.duration;
    }
    const float cycle = blend_cycle_duration(durations.data(), _weights.data(), (uint32_t)durations.size());
    if (cycle <= 0.0f) return;
    if (playing) phase += p_dt / cycle;
    phase -= std::floor(phase);

    std::vector<PoseInput> inputs;
    _sample_poses.resize(space.samples.size());
    for (size_t i = 0; i < space.samples.size(); i++) {
        if (_weights[i] <= 0.0f) continue;
        const LoadedClip* c = clip(ctx, space.samples[i].clip);
        if (!c) continue;
        _sample_poses[i] = skeleton.rest;
        clip_sample(c->clip, c->map, phase * c->clip.duration, _sample_poses[i].data());
        inputs.push_back({ _sample_poses[i].data(), _weights[i] });
    }
    _pose = skeleton.rest;
    pose_blend(inputs.data(), (uint32_t)inputs.size(), joints, _pose.data());
    rig_preview.pose(_pose.data());
}

}
