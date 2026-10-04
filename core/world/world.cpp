#include <core/world/world.h>

namespace lumen {

    
Error World::initialize()
{
    using enum Error;
    
    return OK;
}

void World::shutdown()
{
    pools.clear();
    generations.clear();
    free_list.clear();
}
    
Error World::load()
{
    using enum Error;

    const vec3 target = vec3(0.0f, 50.0f, 0.0f);
    const float radius = 1000.0f;
    const float height = 50.0f;
    const float angle = 0.0f;
    const vec3 eye = target + vec3(radius * std::cos(angle), height, -radius * std::sin(angle));
    default_camera.position = eye;
    default_camera.rotation = quatLookAt(normalize(target - eye), vec3(0.0f, 1.0f, 0.0f));
    active_camera = &default_camera;
    
    return OK;
}

void World::unload()
{

}
    
Entity World::create()
{
    if (!free_list.empty()) {
        uint32_t i = free_list.back();
        free_list.pop_back();
        return { i, generations[i] };
    }
    uint32_t i = (uint32_t)generations.size();
    generations.push_back(0);
    return { i, 0 };
}

void World::destroy(Entity e)
{
    if (!valid(e)) return;
    for (auto& p : pools) { if (p) p->_remove(e); }
    generations[e.index]++; // invalidates every outstanding handle to this index
    free_list.push_back(e.index);
}

}