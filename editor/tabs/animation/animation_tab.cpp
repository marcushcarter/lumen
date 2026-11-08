#include <editor/tabs/animation/animation_tab.h>
#include <core/assets/asset_common.h>
#include <core/io/file_io.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <editor/assets/asset_registry.h>
#include <editor/tabs/animation/panels/animation_details_panel.h>
#include <editor/tabs/animation/panels/animation_timeline_panel.h>
#include <imgui.h>
#include <toml++/toml.hpp>
#include <sstream>

namespace lumen {

Error AnimationTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    _refresh_title();

    toml::table tbl;
    try { tbl = toml::parse_file(path.string()); }
    catch (const toml::parse_error&) {
        log_write("Animation Tab: failed to read '%s'.", path.string().c_str());
        return FAILED;
    }
    auto section = tbl[asset_type_section(AssetType::ANIMATION)];
    loop = section["loop"].value_or(true);
    preview_mesh = Guid::from_string(section["preview_mesh"].value_or(std::string()));

    _poll_clip(ctx, true);
    if (!preview_mesh.valid()) preview_mesh = RigPreview::find_mesh(ctx, clip.skeleton);
    saved_loop = loop;
    saved_preview_mesh = preview_mesh;

    _create_layout(ctx, "AnimationHost");
    layout.add_panel<AnimationDetailsPanel>()->tab = this;
    layout.add_panel<AnimationTimelinePanel>()->tab = this;
    layout.viewport.overlay = [this](ImDrawList* p_draw_list, ImVec2 p_pos, ImVec2 p_size) { rig_preview.draw_bones(p_draw_list, p_pos, p_size, orbit.camera); };
    layout.viewport.options_controls = [this] { rig_preview.draw_controls(); };

    _create_preview();
    preview.remove<StaticTag>(_preview_entity);
    preview.add<PoseComponent>(_preview_entity);

    orbit.update();
    return OK;
}

Error AnimationTab::save(EditorContext& ctx)
{
    using enum Error;
    (void)ctx;
    if (!dirty()) return OK;

    toml::table tbl;
    try { tbl = toml::parse_file(path.string()); }
    catch (const toml::parse_error&) { return FAILED; }
    toml::table* section = tbl[asset_type_section(AssetType::ANIMATION)].as_table();
    if (!section) return FAILED;
    section->insert_or_assign("loop", loop);
    if (preview_mesh.valid()) section->insert_or_assign("preview_mesh", preview_mesh.to_string());
    else section->erase("preview_mesh");
    std::ostringstream ss;
    ss << tbl;
    if (!write_file_atomic(path, ss.str())) {
        log_write("Animation Tab: failed to save '%s'.", path.string().c_str());
        return FAILED;
    }

    saved_loop = loop;
    saved_preview_mesh = preview_mesh;
    _refresh_title();
    return OK;
}

void AnimationTab::on_update(EditorContext& ctx, float p_dt)
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

void AnimationTab::_poll_clip(EditorContext& ctx, bool p_force)
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
        log_write("Animation Tab: failed to load the clip of '%s'.", path.string().c_str());
        clip = {};
    }
    player.time = std::min(player.time, clip.duration);
    _mapped_version = UINT64_MAX;
}

void AnimationTab::_sync_preview(EditorContext& ctx)
{
    if (preview_mesh != _shown_mesh && ctx.renderer) {
        _shown_mesh = preview_mesh;
        ctx.renderer->models.set(_preview_model, preview_mesh, {});
        _framed = false;
    }
    rig_preview.refresh(ctx, preview_mesh, false);
    if (rig_preview.version == _mapped_version) return;
    _mapped_version = rig_preview.version;
    clip_track_map(rig_preview.skeleton_data(), clip, _track_map);
    missing_tracks = 0;
    for (int32_t j : _track_map) {
        if (j == SKELETON_NO_JOINT) missing_tracks++;
    }
}

void AnimationTab::_pose()
{
    const LSkeleton& skeleton = rig_preview.skeleton_data();
    if (skeleton.joint_count() == 0) return;
    _local = skeleton.rest;
    clip_sample(clip, _track_map, player.time, _local.data());
    rig_preview.pose(_local.data());
}

}
