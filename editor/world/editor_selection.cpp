#include <editor/world/editor_selection.h>
#include <editor/world/editor_folders.h>
#include <algorithm>

namespace lumen {

void EditorSelection::clear()
{
    if (empty()) return;
    for (const Entity e : entities) entity_marks[e.index] = 0;
    for (const uint32_t f : folders) folder_marks[f] = 0;
    entities.clear();
    folders.clear();
    version++;
}

void EditorSelection::select(Entity p_entity)
{
    clear();
    add(p_entity);
}

void EditorSelection::add(Entity p_entity)
{
    if (p_entity == ENTITY_NULL) return;
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

void EditorSelection::add_folder(uint32_t p_folder)
{
    if (contains_folder(p_folder)) {
        if (folders.back() == p_folder) return;
        folders.erase(std::find(folders.begin(), folders.end(), p_folder));
    } else {
        if (p_folder >= folder_marks.size()) folder_marks.resize(p_folder + 1, 0);
        folder_marks[p_folder] = 1;
    }
    folders.push_back(p_folder);
    version++;
}

void EditorSelection::remove_folder(uint32_t p_folder)
{
    if (!contains_folder(p_folder)) return;
    folder_marks[p_folder] = 0;
    folders.erase(std::find(folders.begin(), folders.end(), p_folder));
    version++;
}

void EditorSelection::toggle_folder(uint32_t p_folder)
{
    if (contains_folder(p_folder)) remove_folder(p_folder);
    else add_folder(p_folder);
}

void EditorSelection::prune(const World& world, const EditorFolders& p_folders)
{
    const size_t entity_count = entities.size();
    const size_t folder_count = folders.size();
    std::erase_if(entities, [&](Entity e) {
        if (world.valid(e)) return false;
        if (entity_marks[e.index] == e.generation + 1) entity_marks[e.index] = 0;
        return true;
    });
    std::erase_if(folders, [&](uint32_t f) {
        if (p_folders.valid(f)) return false;
        folder_marks[f] = 0;
        return true;
    });
    if (entities.size() != entity_count || folders.size() != folder_count) version++;
}

}