#include <editor/tabs/world/scene/editor_spawn.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <editor/application/editor_context.h>
#include <core/world/components.h>
#include <core/rendering/renderer.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <cstdio>

namespace lumen {

Entity editor_spawn(EditorContext& ctx, SpawnType p_type, Entity p_parent, Guid p_model)
{
    World& world = *ctx.world;
    const Camera& cam = ctx.renderer->main_view.camera;

    const Entity e = world.create_persistent(Guid::generate());
    NameComponent name{};
    snprintf(name.name, NameComponent::MAX_LENGTH, "%s", SPAWN_NAMES[(int)p_type]);
    world.add<NameComponent>(e, name);

    if (p_type != SpawnType::FOLDER) {
        TransformComponent xf;
        xf.position = cam.position + cam.forward() * SPAWN_DISTANCE;
        world.add<TransformComponent>(e, xf);
    }

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
