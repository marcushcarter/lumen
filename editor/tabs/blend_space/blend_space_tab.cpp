#include <editor/tabs/blend_space/blend_space_tab.h>
#include <core/rendering/debug_draw.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <editor/tabs/blend_space/panels/blend_space_details_panel.h>
#include <imgui.h>

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
    layout.viewport.debug_lines = [this] { rig_preview.draw_bones(debug_draw()); };

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
    layout.viewport.has_bones = rig_preview.skinned;
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
    return anim.clip(ctx, rig_preview, p_guid);
}

Guid BlendSpaceTab::preview_mesh(EditorContext& ctx)
{
    return anim.blend_space_mesh(ctx, rig_preview, space);
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
    if (skeleton.joint_count() == 0) return;

    const float advance = playing ? p_dt : 0.0f;
    _pose = skeleton.rest;
    if (!anim.blend_pose(ctx, rig_preview, space, preview_value, advance, phase, _pose.data())) return;
    overlay_time += advance;
    for (Guid g : preview_overlays) {
        if (const LOverlay* o = anim.overlay(ctx, g)) anim.overlay_apply(ctx, rig_preview, *o, overlay_time, 1.0f, _pose.data());
    }
    rig_preview.pose(_pose.data());
}

}
