#include <editor/play/editor_play.h>
#include <core/scene/components.h>
#include <editor/viewport/viewport.h>
#include <algorithm>
#include <cstring>

namespace lumen {

Error EditorPlay::initialize()
{
    using enum Error;
    Error err = runtime.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    orbit_camera_system.physics = &physics_system;
    session.systems = { &physics_system, &orbit_camera_system, &orbit_camera_system.late };
    return runtime.load();
}

void EditorPlay::shutdown()
{
    stop();
    runtime.shutdown();
}

void EditorPlay::start(Scene& r_authored, Guid p_world, const Camera& p_camera)
{
    stop();
    authored = &r_authored;
    scene_guid = p_world;
    _clone();
    authored->changes_clear();
    authored->track_changes = true;
    session.begin(runtime, p_camera);
    log_write("Play: started.");
}

void EditorPlay::stop()
{
    if (!active()) return;
    scaled = false;
    scaled_from_pause = false;
    session.end();
    runtime.unload();
    if (authored) {
        authored->track_changes = false;
        authored->changes_clear();
    }
    authored = nullptr;
    scene_guid = Guid{};
    log_write("Play: stopped.");
}

void EditorPlay::toggle()
{
    scaled = false;
    scaled_from_pause = false;
    if (playing()) session.pause();
    else if (editing()) session.resume();
}

void EditorPlay::step()
{
    if (editing()) step_pending = true;
}

void EditorPlay::set_scaled(bool p_on)
{
    if (!active() || p_on == scaled) return;
    scaled = p_on;
    if (p_on) {
        scaled_from_pause = editing();
        if (scaled_from_pause) session.resume();
        return;
    }
    if (scaled_from_pause && playing()) session.pause();
    scaled_from_pause = false;
}

void EditorPlay::update(float p_dt)
{
    simulated_dt = 0.0f;
    if (!active()) return;
    _mirror();
    if (step_pending && editing()) {
        step_pending = false;
        session.resume();
        session.update(STEP_DT);
        session.pause();
        simulated_dt = STEP_DT;
        return;
    }
    step_pending = false;
    if (scaled && playing()) {
        speed = std::clamp(speed, MIN_SPEED, MAX_SPEED);
        p_dt *= speed;
    }
    session.update(p_dt);
    if (playing()) simulated_dt = p_dt;
}

Entity EditorPlay::to_runtime(Entity p_authored) const
{
    if (!authored) return ENTITY_NULL;
    const EntityIdComponent* id = authored->try_get<EntityIdComponent>(p_authored);
    return id ? runtime.find(id->guid) : ENTITY_NULL;
}

Entity EditorPlay::to_authored(uint32_t p_runtime_index) const
{
    if (!authored) return ENTITY_NULL;
    const EntityIdComponent* id = runtime.try_get<EntityIdComponent>(runtime.entity_at(p_runtime_index));
    return id ? authored->find(id->guid) : ENTITY_NULL;
}

void EditorPlay::_sync_entity(Entity p_authored)
{
    const EntityIdComponent* id = authored->try_get<EntityIdComponent>(p_authored);
    if (!id) return;
    Entity target = runtime.find(id->guid);
    if (target == ENTITY_NULL) target = runtime.create_persistent(id->guid);
    if (target == ENTITY_NULL) return;
    const uint32_t count = (uint32_t)std::min(authored->components.size(), runtime.components.size());
    for (uint32_t i = 0; i < count; i++) {
        const ComponentInfo& info = authored->components[i];
        if (info.sync && info.key && !info.editor_only) info.sync(*authored, p_authored, runtime, target);
    }
}

void EditorPlay::_sync_environment()
{
    if (authored->shadows != _shadows) {
        _shadows = authored->shadows;
        runtime.shadows = authored->shadows;
    }
    if (authored->physics != _physics) {
        _physics = authored->physics;
        runtime.physics = authored->physics;
    }
    if (std::memcmp(&authored->environment, &_environment, sizeof(EnvironmentSettings)) == 0) return;
    _environment = authored->environment;
    runtime.environment = authored->environment;
}

void EditorPlay::_clone()
{
    runtime.unload();
    runtime.load();
    std::memcpy((void*)&_environment, &authored->environment, sizeof(EnvironmentSettings));
    runtime.environment = authored->environment;
    _shadows = authored->shadows;
    runtime.shadows = authored->shadows;
    _physics = authored->physics;
    runtime.physics = authored->physics;
    std::vector<Entity> entities;
    authored->view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) { entities.push_back(p_entity); });
    for (const Entity e : entities) _sync_entity(e);
}

void EditorPlay::_mirror()
{
    Scene& a = *authored;
    if (a.changed_all) {
        a.changes_clear();
        _clone();
        return;
    }
    _sync_environment();
    if (a.changed.empty() && a.destroyed.empty()) return;

    for (const uint64_t guid : a.destroyed) {
        Guid g;
        g.value = guid;
        const Entity e = runtime.find(g);
        if (e != ENTITY_NULL) runtime.destroy(e);
    }

    std::sort(a.changed.begin(), a.changed.end());
    a.changed.erase(std::unique(a.changed.begin(), a.changed.end()), a.changed.end());
    for (const uint32_t index : a.changed) {
        const Entity e = a.entity_at(index);
        if (e != ENTITY_NULL) _sync_entity(e);
    }
    a.changes_clear();
}

}
