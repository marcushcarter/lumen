#pragma once
#include <core/world/world.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct EditorFolders;

struct EditorSelection
{
    std::vector<Entity> entities;
    std::vector<uint32_t> folders;
    std::vector<uint32_t> entity_marks;
    std::vector<uint8_t> folder_marks;
    uint64_t version = 0;

    bool empty() const { return entities.empty() && folders.empty(); }
    uint32_t count() const { return (uint32_t)(entities.size() + folders.size()); }
    Entity primary() const { return entities.empty() ? ENTITY_NULL : entities.back(); }
    uint32_t primary_folder() const { return folders.empty() ? 0xFFFFFFFF : folders.back(); }

    bool contains(Entity p_entity) const {
        return p_entity.index < entity_marks.size() && entity_marks[p_entity.index] == p_entity.generation + 1;
    }
    bool contains_folder(uint32_t p_folder) const { return p_folder < folder_marks.size() && folder_marks[p_folder]; }

    void clear();
    void select(Entity p_entity);
    void add(Entity p_entity);
    void remove(Entity p_entity);
    void toggle(Entity p_entity);
    void add_folder(uint32_t p_folder);
    void remove_folder(uint32_t p_folder);
    void toggle_folder(uint32_t p_folder);
    void prune(const World& world, const EditorFolders& folders);
};

}