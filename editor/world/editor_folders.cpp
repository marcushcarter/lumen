#include <editor/world/editor_folders.h>
#include <core/world/components.h>
#include <cstdio>
#include <cstring>

namespace lumen {

void EditorFolders::clear()
{
    folders.clear();
    free_list.clear();
    version++;
}

uint32_t EditorFolders::create(const char* p_name, uint32_t p_parent)
{
    if (p_parent != ROOT && !valid(p_parent)) p_parent = ROOT;
    uint32_t index;
    if (!free_list.empty()) {
        index = free_list.back();
        free_list.pop_back();
    } else {
        index = (uint32_t)folders.size();
        folders.emplace_back();
    }
    Folder& f = folders[index];
    f = {};
    f.guid = Guid::generate();
    f.parent = p_parent;
    _unique_name(p_parent, p_name, f.name);
    f.alive = true;
    version++;
    return index;
}

void EditorFolders::destroy(World& world, uint32_t p_folder)
{
    if (!valid(p_folder)) return;
    const uint32_t parent = folders[p_folder].parent;
    for (Folder& f : folders) {
        if (f.alive && f.parent == p_folder) f.parent = parent;
    }
    world.view<EditorFolderComponent>([&](Entity, EditorFolderComponent& p_f) {
        if (p_f.folder == p_folder) p_f.folder = parent;
    });
    folders[p_folder].alive = false;
    free_list.push_back(p_folder);
    version++;
}

void EditorFolders::rename(uint32_t p_folder, const char* p_name)
{
    if (!valid(p_folder) || !p_name || !p_name[0]) return;
    snprintf(folders[p_folder].name, MAX_NAME, "%s", p_name);
    version++;
}

bool EditorFolders::set_parent(uint32_t p_folder, uint32_t p_parent)
{
    if (!valid(p_folder)) return false;
    if (p_parent != ROOT && (!valid(p_parent) || is_ancestor(p_folder, p_parent))) return false;
    if (folders[p_folder].parent == p_parent) return false;
    folders[p_folder].parent = p_parent;
    version++;
    return true;
}

bool EditorFolders::is_ancestor(uint32_t p_ancestor, uint32_t p_folder) const
{
    for (uint32_t f = p_folder; f != ROOT && valid(f); f = folders[f].parent) {
        if (f == p_ancestor) return true;
    }
    return false;
}

void EditorFolders::expand_to(uint32_t p_folder)
{
    for (uint32_t f = p_folder; f != ROOT && valid(f); f = folders[f].parent) folders[f].expanded = true;
}

uint32_t EditorFolders::entity_folder(const World& world, Entity p_entity) const
{
    const EditorFolderComponent* f = world.try_get<EditorFolderComponent>(p_entity);
    return f && valid(f->folder) ? f->folder : ROOT;
}

void EditorFolders::entity_set_folder(World& world, Entity p_entity, uint32_t p_folder)
{
    if (!world.valid(p_entity) || (p_folder != ROOT && !valid(p_folder))) return;
    if (EditorFolderComponent* f = world.try_get<EditorFolderComponent>(p_entity)) {
        if (f->folder == p_folder) return;
        f->folder = p_folder;
    } else if (p_folder != ROOT) {
        world.deferred_add<EditorFolderComponent>(p_entity, { p_folder });
    }
    version++;
}

void EditorFolders::_unique_name(uint32_t p_parent, const char* p_base, char (&r_name)[MAX_NAME]) const
{
    auto taken = [&](const char* p_name) {
        for (const Folder& f : folders) {
            if (f.alive && f.parent == p_parent && std::strcmp(f.name, p_name) == 0) return true;
        }
        return false;
    };
    snprintf(r_name, MAX_NAME, "%s", p_base);
    for (uint32_t n = 2; taken(r_name); n++) snprintf(r_name, MAX_NAME, "%s %u", p_base, n);
}

}