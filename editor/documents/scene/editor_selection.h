#pragma once
#include <core/scene/scene.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct EditorSelection
{
    std::vector<Entity> entities;
    std::vector<uint32_t> entity_marks;
    uint64_t version = 0;
    bool environment = false;
    bool terrain = false;
    bool shadows = false;
    int32_t _shape = -1;
    uint64_t _shape_version = 0;

    bool empty() const { return entities.empty(); }
    uint32_t count() const { return (uint32_t)entities.size(); }
    Entity primary() const { return entities.empty() ? ENTITY_NULL : entities.back(); }
    int32_t shape() const { return _shape_version == version ? _shape : -1; }
    void select_shape(int32_t p_shape) { _shape = p_shape; _shape_version = version; }

    bool contains(Entity p_entity) const {
        return p_entity.index < entity_marks.size() && entity_marks[p_entity.index] == p_entity.generation + 1;
    }

    void clear();
    void select(Entity p_entity);
    void select_environment();
    void select_terrain();
    void select_shadows();
    void add(Entity p_entity);
    void remove(Entity p_entity);
    void toggle(Entity p_entity);
    void prune(const Scene& world);
};

}
