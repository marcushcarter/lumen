#include <editor/tabs/locomotion/locomotion_tab.h>
#include <core/rendering/debug_draw.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <editor/tabs/locomotion/panels/locomotion_details_panel.h>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace lumen {

Error LocomotionTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    if (character.load(path) != OK || character.guid != guid) {
        log_write("Locomotion Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = character;
    _refresh_title();

    _create_layout(ctx, "LocomotionHost");
    layout.add_panel<LocomotionDetailsPanel>()->tab = this;
    layout.viewport.debug_lines = [this] { rig_preview.draw_bones(debug_draw()); };

    _create_preview();
    preview.remove<StaticTag>(_preview_entity);
    preview.add<PoseComponent>(_preview_entity);

    orbit.update();
    return OK;
}

Error LocomotionTab::save(EditorContext& ctx)
{
    (void)ctx;
    if (character.save(path) != Error::OK) {
        log_write("Locomotion Tab: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = character;
    _refresh_title();
    return Error::OK;
}

void LocomotionTab::on_update(EditorContext& ctx, float p_dt)
{
    layout.viewport.has_bones = rig_preview.skinned;
    _draw_layout(ctx);
    _update_orbit();
    _sync_preview(ctx);
    _pose_preview(ctx, playing ? p_dt : 0.0f);
    rig_preview.apply(preview, _preview_entity);

    vec3 center;
    float radius;
    if (!_framed && ctx.renderer && rig_preview.bounds(ctx.renderer->geometry, _shown_mesh, center, radius)) {
        orbit.frame(center, radius);
        _framed = true;
    }
    orbit.update();
}

Guid LocomotionTab::preview_mesh(EditorContext& ctx)
{
    for (const CharacterStance* stance : { &character.standing, &character.crouching }) {
        for (Guid g : stance->cycles) {
            if (const LBlendSpace* space = anim.blend_space(ctx, g)) {
                if (const Guid mesh = anim.blend_space_mesh(ctx, rig_preview, *space); mesh.valid()) return mesh;
            }
        }
        if (const AnimPreview::LoadedClip* c = anim.clip(ctx, rig_preview, stance->idle)) {
            if (const Guid mesh = RigPreview::find_mesh(ctx, c->clip.skeleton); mesh.valid()) return mesh;
        }
    }
    return {};
}

void LocomotionTab::_sync_preview(EditorContext& ctx)
{
    const Guid mesh = preview_mesh(ctx);
    if (mesh != _shown_mesh && ctx.renderer) {
        _shown_mesh = mesh;
        ctx.renderer->models.set(_preview_model, mesh, {});
        _framed = false;
    }
    rig_preview.refresh(ctx, _shown_mesh, false);
}

void LocomotionTab::_pose_preview(EditorContext& ctx, float p_dt)
{
    const LSkeleton& skeleton = rig_preview.skeleton_data();
    if (skeleton.joint_count() == 0) return;

    const float d = glm::radians(preview_direction);
    state.velocity = (character_forward(state.yaw) * std::cos(d) + character_right(state.yaw) * std::sin(d)) * preview_speed + vec3(0.0f, state.velocity.y, 0.0f);
    state.speed = preview_speed;
    state.moving = preview_speed > 0.01f;
    state.has_input = state.moving;
    state.rotation_mode = preview_aim ? RotationMode::AIMING : RotationMode::LOOKING_DIRECTION;
    state.aim_yaw = preview_look;
    state.aim_yaw_rate = 0.0f;
    if (state.moving) state.yaw = preview_look;
    _pose = skeleton.rest;
    const bool posed = anim.character_pose(ctx, rig_preview, character, state, p_dt, _pose.data());
    state.yaw = character_wrap(state.yaw + state.turn_delta);
    state.turn_delta = 0.0f;
    state.jumped = false;
    state.landed = false;
    if (!posed) return;
    const quat yaw = angleAxis(glm::radians(state.yaw), vec3(0.0f, 1.0f, 0.0f));
    for (uint32_t j = 0; j < skeleton.joint_count(); j++) {
        if (skeleton.parents[j] >= 0) continue;
        _pose[j].rotation = yaw * _pose[j].rotation;
        _pose[j].translation = yaw * _pose[j].translation;
    }
    rig_preview.pose(_pose.data());
}

}
