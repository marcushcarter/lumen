#include <editor/documents/animation/animation_document.h>
#include <core/game/entity_debug.h>
#include <core/rendering/debug_draw.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/scene/components.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/import/animation_cooker.h>
#include <editor/documents/animation/panels/animation_details_panel.h>
#include <editor/documents/animation/panels/animation_timeline_panel.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace lumen {

Error AnimationDocument::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    _refresh_title();

    toml::table tbl;
    try { tbl = toml::parse_file(path.string()); }
    catch (const toml::parse_error&) {
        log_write("Animation: failed to read '%s'.", path.string().c_str());
        return FAILED;
    }
    auto section = tbl[asset_type_section(AssetType::ANIMATION)];
    loop = section["loop"].value_or(true);
    preview_mesh = Guid::from_string(section["preview_mesh"].value_or(std::string()));
    if (const toml::table* t = section.as_table()) {
        LAnimation authored;
        AnimationCooker::read_authored(*t, authored);
        events = std::move(authored.events);
        curves = std::move(authored.curves);
    }
    saved_events = events;
    saved_curves = curves;

    _poll_clip(ctx, true);
    if (!preview_mesh.valid()) preview_mesh = RigPreview::find_mesh(ctx, clip.skeleton);
    saved_loop = loop;
    saved_preview_mesh = preview_mesh;

    _create_layout();
    layout.add_panel<AnimationDetailsPanel>()->tab = this;
    layout.add_panel<AnimationTimelinePanel>()->tab = this;
    viewport.debug_lines = [this](DebugDraw& r_draw) { _draw_meta(r_draw); };

    _create_preview();
    preview.remove<StaticTag>(_preview_entity);
    preview.add<PoseComponent>(_preview_entity);

    orbit.update();
    return OK;
}

Error AnimationDocument::save(EditorContext& ctx)
{
    using enum Error;
    if (!dirty()) return OK;
    for (ClipCurve& c : curves) std::sort(c.keys.begin(), c.keys.end(), [](const vec2& a, const vec2& b) { return a.x < b.x; });
    std::stable_sort(events.begin(), events.end(), [](const ClipEvent& a, const ClipEvent& b) { return a.time < b.time; });

    toml::table tbl;
    try { tbl = toml::parse_file(path.string()); }
    catch (const toml::parse_error&) { return FAILED; }
    toml::table* section = tbl[asset_type_section(AssetType::ANIMATION)].as_table();
    if (!section) return FAILED;
    section->insert_or_assign("loop", loop);
    if (preview_mesh.valid()) section->insert_or_assign("preview_mesh", preview_mesh.to_string());
    else section->erase("preview_mesh");
    LAnimation authored;
    authored.events = events;
    authored.curves = curves;
    AnimationCooker::write_authored(*section, authored);
    std::ostringstream ss;
    ss << tbl;
    if (!write_file_atomic(path, ss.str())) {
        log_write("Animation: failed to save '%s'.", path.string().c_str());
        return FAILED;
    }

    if ((events != saved_events || curves != saved_curves) && ctx.project && clip.frame_count) {
        clip.events = events;
        clip.curves = curves;
        if (clip.save_cooked(ctx.project->content_path(guid)) != OK) log_write("Animation: failed to write the clip of '%s'.", path.string().c_str());
    }

    saved_loop = loop;
    saved_preview_mesh = preview_mesh;
    saved_events = events;
    saved_curves = curves;
    _refresh_title();
    return OK;
}

void AnimationDocument::on_update(EditorContext& ctx, float p_dt)
{
    _draw_layout(ctx);
    _update_orbit();
    _poll_clip(ctx, false);
    _sync_preview(ctx);

    player.loop = loop;
    player.advance(p_dt, clip.duration);
    _pose();
    rig_preview.apply(preview, _preview_entity);

    vec3 center;
    float radius;
    if (!_framed && ctx.renderer && rig_preview.bounds(ctx.renderer->geometry, preview_mesh, center, radius)) {
        orbit.frame(center, radius);
        _framed = true;
    }
    orbit.update();
}

void AnimationDocument::_poll_clip(EditorContext& ctx, bool p_force)
{
    const double now = ImGui::GetTime();
    if ((!p_force && now - _clip_poll < CLIP_POLL_S) || !ctx.project) return;
    _clip_poll = now;

    const std::filesystem::path bin = ctx.project->content_path(guid);
    std::error_code ec;
    const std::filesystem::file_time_type t = std::filesystem::last_write_time(bin, ec);
    if (!p_force && (ec || t == _clip_time)) return;
    _clip_time = t;

    if (clip.load_cooked(bin) != Error::OK) {
        log_write("Animation: failed to load the clip of '%s'.", path.string().c_str());
        clip = {};
    }
    player.time = std::min(player.time, clip.duration);
    _mapped_version = UINT64_MAX;
}

void AnimationDocument::_sync_preview(EditorContext& ctx)
{
    if (preview_mesh != _shown_mesh && ctx.renderer) {
        _shown_mesh = preview_mesh;
        ctx.renderer->mesh_slots.set(_preview_model, preview_mesh, {});
        _framed = false;
    }
    rig_preview.refresh(ctx, preview_mesh);
    if (rig_preview.version == _mapped_version) return;
    _mapped_version = rig_preview.version;
    clip_track_map(rig_preview.skeleton_data(), clip, _track_map);
    _retarget = {};
    LSkeleton authored;
    if (ctx.project && clip.skeleton.valid() && authored.load_cooked(ctx.project->content_path(clip.skeleton)) == Error::OK) clip_retarget(authored, rig_preview.skeleton_data(), _track_map, _retarget);
    missing_tracks = 0;
    for (int32_t j : _track_map) {
        if (j == SKELETON_NO_JOINT) missing_tracks++;
    }
    for (int s = 0; s < (int)ClipFootSide::COUNT; ++s) {
        const std::string& joint = clip.meta.feet[s].joint;
        _foot_joint[s] = joint.empty() ? SKELETON_NO_JOINT : rig_preview.skeleton_data().find(joint);
    }
}

float AnimationDocument::clip_speed_at(float p_time) const
{
    if (clip.meta.traveled.size() < 2 || clip.sample_rate <= 0.0f) return 0.0f;
    const float step = 1.0f / clip.sample_rate;
    const float a = std::max(0.0f, p_time - step);
    const float b = std::min(clip.duration, p_time + step);
    return b > a ? (clip.traveled_at(b) - clip.traveled_at(a)) / (b - a) : 0.0f;
}

void AnimationDocument::_draw_meta(DebugDraw& r_draw) const
{
    const float scale = preview_scale();
    const std::vector<vec4>& trail = clip.meta.trajectory;
    if (trail.size() > 1) {
        const DebugDraw::Style line{ .color = DebugDraw::CYAN, .xray = true };
        for (size_t i = 1; i < trail.size(); ++i) r_draw.line(vec3(trail[i - 1]) * scale, vec3(trail[i]) * scale, line);
        const vec4 now = clip.trajectory_at(player.time);
        const vec3 at = vec3(now) * scale;
        const vec3 facing(std::sin(now.w), 0.0f, std::cos(now.w));
        r_draw.arrow(at, at + facing * (0.4f * scale), { .color = DebugDraw::YELLOW, .xray = true });
    }
    if (rig_preview._global.empty()) return;
    const uint32_t frame = clip.frame_at(player.time);
    for (int s = 0; s < (int)ClipFootSide::COUNT; ++s) {
        const int32_t j = _foot_joint[s];
        if (j == SKELETON_NO_JOINT || j >= (int32_t)rig_preview._global.size()) continue;
        const vec3 p = vec3(rig_preview._global[j][3]);
        const bool planted = clip.meta.feet[s].planted(frame);
        const uint32_t color = planted ? DebugDraw::GREEN : (s == 0 ? DebugDraw::RED : DebugDraw::BLUE);
        r_draw.point(p, planted ? 7.0f : 4.0f, { .color = color, .xray = true });
        if (planted) r_draw.circle(vec3(p.x, p.y, p.z), vec3(0.0f, 1.0f, 0.0f), 0.08f * scale, { .color = DebugDraw::GREEN, .xray = true });
    }
}

void AnimationDocument::_pose()
{
    const LSkeleton& skeleton = rig_preview.skeleton_data();
    if (skeleton.joint_count() == 0) return;
    _local = skeleton.rest;
    clip_sample(BoundClip{ &clip, &_track_map, &_retarget }, player.time, _local.data());
    rig_preview.pose(_local.data());
}

}
