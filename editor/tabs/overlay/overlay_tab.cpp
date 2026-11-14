#include <editor/tabs/overlay/overlay_tab.h>
#include <core/rendering/debug_draw.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <editor/tabs/overlay/panels/overlay_details_panel.h>
#include <imgui.h>

namespace lumen {

Error OverlayTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    if (overlay.load(path) != OK || overlay.guid != guid) {
        log_write("Overlay Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = overlay;
    _refresh_title();

    _create_layout(ctx, "OverlayHost");
    layout.add_panel<OverlayDetailsPanel>()->tab = this;
    layout.viewport.debug_lines = [this] {
        const bool weighted = mask.size() == rig_preview.skeleton_data().joint_count();
        rig_preview.draw_bones(debug_draw(), weighted ? mask.data() : nullptr);
    };

    _create_preview();
    preview.remove<StaticTag>(_preview_entity);
    preview.add<PoseComponent>(_preview_entity);

    orbit.update();
    return OK;
}

Error OverlayTab::save(EditorContext& ctx)
{
    (void)ctx;
    if (overlay.save(path) != Error::OK) {
        log_write("Overlay Tab: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = overlay;
    _refresh_title();
    return Error::OK;
}

void OverlayTab::on_update(EditorContext& ctx, float p_dt)
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

const LBlendSpace* OverlayTab::blend_space(EditorContext& ctx)
{
    return anim.blend_space(ctx, overlay.preview_blend_space);
}

Guid OverlayTab::preview_mesh(EditorContext& ctx)
{
    if (const LBlendSpace* space = blend_space(ctx)) {
        if (const Guid mesh = anim.blend_space_mesh(ctx, rig_preview, *space); mesh.valid()) return mesh;
    }
    const AnimPreview::LoadedClip* c = anim.clip(ctx, rig_preview, overlay.source);
    if (!c) return {};
    if (c->clip.skeleton != _found_skeleton) {
        _found_skeleton = c->clip.skeleton;
        _found_mesh = RigPreview::find_mesh(ctx, _found_skeleton);
    }
    return _found_mesh;
}

void OverlayTab::_sync_preview(EditorContext& ctx)
{
    const Guid mesh = preview_mesh(ctx);
    if (mesh != _shown_mesh && ctx.renderer) {
        _shown_mesh = mesh;
        ctx.renderer->models.set(_preview_model, mesh, {});
        _framed = false;
    }
    rig_preview.refresh(ctx, _shown_mesh, false);
}

void OverlayTab::_pose_preview(EditorContext& ctx, float p_dt)
{
    const LSkeleton& skeleton = rig_preview.skeleton_data();
    anim.mask_weights(overlay, rig_preview, mask);
    if (skeleton.joint_count() == 0) return;

    const float advance = playing ? p_dt : 0.0f;
    time += advance;
    _pose = skeleton.rest;
    if (const LBlendSpace* space = blend_space(ctx)) anim.blend_pose(ctx, rig_preview, *space, preview_value, advance, phase, _pose.data());
    anim.overlay_apply(ctx, rig_preview, overlay, time, preview_weight, _pose.data());
    rig_preview.pose(_pose.data());
}

}
