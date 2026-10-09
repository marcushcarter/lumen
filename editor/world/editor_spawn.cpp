// editor/world/editor_spawn.cpp
#include <editor/world/editor_spawn.h>
#include <editor/world/editor_folders.h>
#include <editor/world/editor_selection.h>
#include <editor/editor_context.h>
#include <core/world/components.h>
#include <core/rendering/renderer.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <cstdio>

namespace lumen {

// the single creation path: outliner menu, toolbar, asset drop and duplicate all end here
Entity editor_spawn(EditorContext& ctx, SpawnType p_type, uint32_t p_folder, Guid p_mesh)
{
    World& world = *ctx.world;
    const Camera& cam = ctx.renderer->active_camera;

    const Entity e = world.create_persistent(Guid::generate());
    TransformComponent xf;
    xf.position = cam.position + cam.forward() * SPAWN_DISTANCE;
    world.add<TransformComponent>(e, xf);

    NameComponent name{};
    snprintf(name.name, NameComponent::MAX_LENGTH, "%s", SPAWN_NAMES[(int)p_type]);
    world.add<NameComponent>(e, name);

    switch (p_type) {
        case SpawnType::MESH:
            world.add<MeshComponent>(e, { p_mesh });
            world.add<StaticTag>(e);
            break;
        case SpawnType::MESH_GRID: {
            MeshGridComponent grid;
            grid.mesh = p_mesh;
            world.add<MeshGridComponent>(e, grid);
            world.add<StaticTag>(e);
        } break;
        default:
            break;
    }

    if (p_folder != EditorFolders::ROOT) ctx.folders->entity_set_folder(world, e, p_folder);
    ctx.selection->select(e);
    return e;
}

Entity editor_spawn_menu_items(EditorContext& ctx, uint32_t p_folder)
{
    Entity e = ENTITY_NULL;
    if (ImGui::MenuItem(ICON_FA_CIRCLE_DOT "  Empty")) e = editor_spawn(ctx, SpawnType::EMPTY, p_folder);
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_CUBE "  Mesh")) e = editor_spawn(ctx, SpawnType::MESH, p_folder);
    if (ImGui::MenuItem(ICON_FA_TABLE_CELLS "  Mesh Grid")) e = editor_spawn(ctx, SpawnType::MESH_GRID, p_folder);
    return e;
}

}