#include <editor/tabs/world/scene/editor_selection.h>
#include <algorithm>

namespace lumen {

void EditorSelection::clear()
{
    if (empty() && !environment) return;
    for (const Entity e : entities) entity_marks[e.index] = 0;
    entities.clear();
    environment = false;
    version++;
}

void EditorSelection::select(Entity p_entity)
{
    clear();
    add(p_entity);
}

void EditorSelection::select_environment()
{
    clear();
    environment = true;
    version++;
}

void EditorSelection::add(Entity p_entity)
{
    if (p_entity == ENTITY_NULL) return;
    if (environment) {
        environment = false;
        version++;
    }
    if (contains(p_entity)) {
        if (entities.back() == p_entity) return;
        entities.erase(std::find(entities.begin(), entities.end(), p_entity));
    } else {
        if (p_entity.index >= entity_marks.size()) entity_marks.resize(p_entity.index + 1, 0);
        entity_marks[p_entity.index] = p_entity.generation + 1;
    }
    entities.push_back(p_entity);
    version++;
}

void EditorSelection::remove(Entity p_entity)
{
    if (!contains(p_entity)) return;
    entity_marks[p_entity.index] = 0;
    entities.erase(std::find(entities.begin(), entities.end(), p_entity));
    version++;
}

void EditorSelection::toggle(Entity p_entity)
{
    if (contains(p_entity)) remove(p_entity);
    else add(p_entity);
}

void EditorSelection::prune(const World& world)
{
    const size_t count = entities.size();
    std::erase_if(entities, [&](Entity e) {
        if (world.valid(e)) return false;
        if (entity_marks[e.index] == e.generation + 1) entity_marks[e.index] = 0;
        return true;
    });
    if (entities.size() != count) version++;
}

}
