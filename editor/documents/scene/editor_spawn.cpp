#include <editor/documents/scene/editor_spawn.h>
#include <editor/documents/scene/editor_selection.h>
#include <editor/application/editor_context.h>
#include <core/scene/components.h>
#include <core/rendering/renderer.h>
#include <core/assets/lmesh.h>
#include <core/project/project.h>
#include <editor/assets/asset_registry.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <cstdio>

namespace lumen {

static bool _mesh_skinned(EditorContext& ctx, Guid p_mesh)
{
    if (!ctx.project || !p_mesh.valid()) return false;
    LMeshRig rig;
    return lmesh_read_rig(ctx.project->content_path(p_mesh), rig);
}

Entity editor_spawn(EditorContext& ctx, SpawnType p_type, Entity p_parent, Guid p_mesh, const vec3* p_position)
{
    Scene& world = *ctx.scene;
    const Camera& cam = ctx.renderer->main_view.camera;

    const Entity e = world.create_persistent(Guid::generate());
    NameComponent name{};
    snprintf(name.name, NameComponent::MAX_LENGTH, "%s", SPAWN_NAMES[(int)p_type]);
    world.add<NameComponent>(e, name);

    if (p_type != SpawnType::FOLDER) {
        TransformComponent xf;
        xf.position = p_position ? *p_position : cam.position + cam.forward() * SPAWN_DISTANCE;
        world.add<TransformComponent>(e, xf);
    }

    switch (p_type) {
        case SpawnType::MESH:
            world.add<MeshComponent>(e, { p_mesh });
            if (!_mesh_skinned(ctx, p_mesh)) world.add<StaticTag>(e);
            world.add<CollisionComponent>(e, {});
            break;
        default:
            break;
    }

    if (world.valid(p_parent)) {
        world.set_parent(e, p_parent);
        if (!world.has<EditorExpandedTag>(p_parent)) world.add<EditorExpandedTag>(p_parent);
    }
    ctx.selection->select(e);
    return e;
}

Entity editor_spawn_menu_items(EditorContext& ctx, Entity p_parent)
{
    Entity e = ENTITY_NULL;
    if (ImGui::MenuItem(ICON_FA_FOLDER "  Folder")) e = editor_spawn(ctx, SpawnType::FOLDER, p_parent);
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_CIRCLE_DOT "  Empty")) e = editor_spawn(ctx, SpawnType::EMPTY, p_parent);
    return e;
}

}
