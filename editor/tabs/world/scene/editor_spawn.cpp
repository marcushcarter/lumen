#include <editor/tabs/world/scene/editor_spawn.h>
#include <editor/tabs/world/scene/editor_folders.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <editor/editor_context.h>
#include <core/world/components.h>
#include <core/rendering/renderer.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <cstdio>

namespace lumen {

Entity editor_spawn(EditorContext& ctx, SpawnType p_type, uint32_t p_folder, Guid p_model)
{
    World& world = *ctx.world;
    const Camera& cam = ctx.renderer->main_view.camera;

    const Entity e = world.create_persistent(Guid::generate());
    TransformComponent xf;
    xf.position = cam.position + cam.forward() * SPAWN_DISTANCE;
    world.add<TransformComponent>(e, xf);

    NameComponent name{};
    snprintf(name.name, NameComponent::MAX_LENGTH, "%s", SPAWN_NAMES[(int)p_type]);
    world.add<NameComponent>(e, name);

    switch (p_type) {
        case SpawnType::MODEL:
            world.add<ModelComponent>(e, { p_model });
            world.add<StaticTag>(e);
            break;
        case SpawnType::MODEL_GRID: {
            ModelGridComponent grid;
            grid.model = p_model;
            world.add<ModelGridComponent>(e, grid);
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
    if (ImGui::MenuItem(ICON_FA_CUBES "  Model")) e = editor_spawn(ctx, SpawnType::MODEL, p_folder);
    if (ImGui::MenuItem(ICON_FA_TABLE_CELLS "  Model Grid")) e = editor_spawn(ctx, SpawnType::MODEL_GRID, p_folder);
    return e;
}

}