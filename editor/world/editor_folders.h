#pragma once
#include <core/world/world.h>
#include <core/assets/guid.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct EditorFolders
{
    static constexpr uint32_t ROOT = EditorFolderComponent::ROOT;
    static constexpr uint32_t MAX_NAME = 64;

    struct Folder
    {
        Guid guid;
        uint32_t parent = ROOT;
        char name[MAX_NAME] = {};
        bool alive = false;
        bool expanded = true;
    };

    std::vector<Folder> folders;
    std::vector<uint32_t> free_list;
    uint64_t version = 0;

    void clear();

    bool valid(uint32_t p_folder) const { return p_folder < folders.size() && folders[p_folder].alive; }
    uint32_t create(const char* p_name, uint32_t p_parent);
    void destroy(World& world, uint32_t p_folder);
    void rename(uint32_t p_folder, const char* p_name);
    bool set_parent(uint32_t p_folder, uint32_t p_parent);
    bool is_ancestor(uint32_t p_ancestor, uint32_t p_folder) const;
    void expand_to(uint32_t p_folder);

    uint32_t entity_folder(const World& world, Entity p_entity) const;
    void entity_set_folder(World& world, Entity p_entity, uint32_t p_folder);

    void _unique_name(uint32_t p_parent, const char* p_base, char (&r_name)[MAX_NAME]) const;
};

}