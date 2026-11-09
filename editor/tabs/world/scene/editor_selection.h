#pragma once
#include <core/world/world.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct EditorSelection
{
    std::vector<Entity> entities;
    std::vector<uint32_t> entity_marks;
    uint64_t version = 0;
    bool environment = false;

    bool empty() const { return entities.empty(); }
    uint32_t count() const { return (uint32_t)entities.size(); }
    Entity primary() const { return entities.empty() ? ENTITY_NULL : entities.back(); }

    bool contains(Entity p_entity) const {
        return p_entity.index < entity_marks.size() && entity_marks[p_entity.index] == p_entity.generation + 1;
    }

    void clear();
    void select(Entity p_entity);
    void select_environment();
    void add(Entity p_entity);
    void remove(Entity p_entity);
    void toggle(Entity p_entity);
    void prune(const World& world);
};

}
