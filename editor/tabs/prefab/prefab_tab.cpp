#include <editor/tabs/prefab/prefab_tab.h>
#include <core/base/hash.h>
#include <core/rendering/renderer.h>
#include <core/world/components.h>
#include <core/world/world_file.h>
#include <editor/tabs/tab_manager.h>
#include <editor/tabs/world/panels/details_panel.h>
#include <editor/tabs/world/panels/outliner_panel.h>
#include <editor/tabs/world/scene/editor_spawn.h>
#include <editor/tabs/world/world_tab.h>
#include <editor/assets/asset_registry.h>
#include <core/project/project.h>
#include <string>
#include <editor/ui/tab_toolbar.h>
#include <imgui.h>
#include <algorithm>
#include <cfloat>

namespace lumen {

static constexpr uint32_t FRAME_ATTEMPTS = 240;

Error PrefabTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    preview.initialize();
    preview.load();
    if (_load(ctx) != OK) return FAILED;
    _refresh_title();

    _create_layout(ctx, "PrefabHost");
    layout.add_panel<OutlinerPanel>();
    details = layout.add_panel<DetailsPanel>();
    layout.viewport.allow_pick = true;
    layout.viewport.allow_outline = true;
    layout.viewport.allow_gizmo = true;
    layout.viewport.allow_asset_drop = true;
    layout.viewport.allow_debug_draw = true;
    selection.version = hash_splitmix(guid.value) & ~0xFFFFFFFFull;
    selection_version = selection.version;
    orbit.update();
    return OK;
}

Error PrefabTab::_load(EditorContext& ctx)
{
    using enum Error;
    (void)ctx;
    preview.unload();
    selection.clear();
    WorldFile file;
    if (WorldFile::read(path, preview, true, file) != OK || file.type != AssetType::PREFAB || file.guid != guid) {
        log_write("Prefab Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    root = file.root;
    _fix_roots();
    _hash = _saved_hash = WorldFile::hash(preview);
    _framed = false;
    _frame_attempts = 0;
    return OK;
}

void PrefabTab::_fix_roots()
{
    std::vector<Entity> roots;
    preview.view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) {
        if (preview.parent(p_entity) == ENTITY_NULL) roots.push_back(p_entity);
    });
    std::sort(roots.begin(), roots.end(), [](Entity a, Entity b) { return a.index < b.index; });
    Entity main = preview.find(root);
    if (main == ENTITY_NULL || preview.parent(main) != ENTITY_NULL) main = roots.empty() ? ENTITY_NULL : roots.front();
    root = main != ENTITY_NULL ? preview.try_get<EntityIdComponent>(main)->guid : Guid{};
    for (const Entity e : roots) {
        if (e != main) preview.set_parent(e, main);
    }
}

void PrefabTab::reload(EditorContext& ctx)
{
    const OrbitCamera keep = orbit;
    if (_load(ctx) != Error::OK) return;
    orbit = keep;
    _framed = true;
}

Error PrefabTab::save(EditorContext& ctx)
{
    using enum Error;
    preview.deferred_flush();
    _fix_roots();
    WorldFile file;
    file.guid = guid;
    file.root = root;
    file.type = AssetType::PREFAB;
    if (WorldFile::write(path, preview, file) != OK) {
        log_write("Prefab Tab: failed to save '%s'.", path.string().c_str());
        return FAILED;
    }
    _hash = _saved_hash = WorldFile::hash(preview);
    _refresh_title();
    prefab_changed(ctx, guid, prefab_library().take(guid), this);
    return OK;
}

void PrefabTab::prefab_changed(EditorContext& ctx, Guid p_prefab, std::unique_ptr<Prefab> p_old, const PrefabTab* p_source)
{
    Prefab* fresh = prefab_library().get(p_prefab);
    if (ctx.main_world) prefab_refresh(*ctx.main_world, p_prefab, p_old.get(), fresh, true);
    if (!ctx.tabs) return;
    for (EditorTab* t : ctx.tabs->tabs) {
        if (t->kind != KIND || t == p_source || t->guid != p_prefab) continue;
        PrefabTab* tab = static_cast<PrefabTab*>(t);
        if (tab->dirty()) log_write("Prefab: '%s' changed while its tab has unsaved edits; save or reopen the tab to pick it up.", tab->path.filename().string().c_str());
        else tab->reload(ctx);
    }
}

void PrefabTab::_frame(EditorContext& ctx)
{
    if (_framed || !ctx.renderer) return;
    if (++_frame_attempts > FRAME_ATTEMPTS) {
        _framed = true;
        return;
    }
    vec3 lo(FLT_MAX);
    vec3 hi(-FLT_MAX);
    bool any = false;
    bool pending = false;
    preview.view<ModelComponent>([&](Entity p_entity, const ModelComponent& p_model) {
        const ModelCache::Entry* entry = ctx.renderer->models.get(p_model.model);
        if (!entry || !entry->mesh.valid()) return;
        const LMesh* mesh = ctx.renderer->geometry.get(ctx.renderer->geometry.find(entry->mesh));
        if (!mesh) {
            pending = true;
            return;
        }
        const mat4 m = preview.world_matrix(p_entity);
        const float s = std::max(length(vec3(m[0])), std::max(length(vec3(m[1])), length(vec3(m[2]))));
        const vec3 c = vec3(m * vec4(vec3(mesh->bounds_sphere), 1.0f));
        const vec3 r(mesh->bounds_sphere.w * s);
        lo = min(lo, c - r);
        hi = max(hi, c + r);
        any = true;
    });
    if (pending) return;
    if (any) orbit.frame((lo + hi) * 0.5f, std::max(length(hi - lo) * 0.5f, 0.1f));
    _framed = true;
}

void PrefabTab::_draw_toolbar(EditorContext& ctx)
{
    TabToolbar bar;
    bar.begin();
    bar.save(ctx, *this);
    bar.gap();
    if (bar.menu("Select")) {
        if (ImGui::MenuItem("All")) {
            selection.clear();
            preview.view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) {
                if (!preview.has<EditorHiddenTag>(p_entity)) selection.add(p_entity);
            });
        }
        if (ImGui::MenuItem("None")) selection.clear();
        bar.end_menu();
    }
    if (bar.menu("Add")) {
        Entity parent = ENTITY_NULL;
        const Entity primary = selection.primary();
        if (preview.valid(primary)) parent = preview.is_folder(primary) ? primary : preview.parent(primary);
        if (parent == ENTITY_NULL) parent = preview.find(root);
        const Entity e = editor_spawn_menu_items(ctx, parent);
        if (e != ENTITY_NULL) selection.select(e);
        bar.end_menu();
    }
    bar.end();
}

void PrefabTab::on_tick(EditorContext& ctx)
{
    AssetTab::on_tick(ctx);
    const double now = ImGui::GetTime();
    if (now - _hash_poll < DIRTY_POLL_S) return;
    _hash_poll = now;
    _hash = WorldFile::hash(preview);
}

void PrefabTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    World* world = ctx.world;
    EditorSelection* scene_selection = ctx.selection;
    PrefabTab* scene_prefab = ctx.prefab_tab;
    ctx.world = &preview;
    ctx.selection = &selection;
    ctx.prefab_tab = this;

    selection.prune(preview);
    if (selection.version != selection_version) {
        selection_version = selection.version;
        if (!selection.empty() && details) layout.well(details->default_zone()).active_name = details->layout_id;
    }
    _draw_layout(ctx);
    preview.deferred_flush();

    ctx.world = world;
    ctx.selection = scene_selection;
    ctx.prefab_tab = scene_prefab;

    _update_orbit();
    _frame(ctx);
    orbit.update();
}

/*****************/
/**** ACTIONS ****/
/*****************/

Entity editor_spawn_prefab(EditorContext& ctx, Guid p_prefab, Entity p_parent)
{
    Prefab* prefab = prefab_library().get(p_prefab);
    if (!prefab || !ctx.world || !ctx.renderer) return ENTITY_NULL;
    World& world = *ctx.world;
    const Camera& cam = ctx.renderer->main_view.camera;
    TransformComponent xf;
    xf.position = cam.position + cam.forward() * SPAWN_DISTANCE;
    const Entity e = prefab_spawn(world, *prefab, ENTITY_NULL, xf);
    if (e == ENTITY_NULL) return e;
    if (world.valid(p_parent)) {
        world.set_parent(e, p_parent);
        if (!world.has<EditorExpandedTag>(p_parent)) world.add<EditorExpandedTag>(p_parent);
    }
    if (ctx.selection) ctx.selection->select(e);
    return e;
}

static std::string _file_stem(const char* p_name)
{
    std::string out;
    for (const char* c = p_name; *c; ++c) {
        const bool bad = *c == '<' || *c == '>' || *c == ':' || *c == '"' || *c == '/' || *c == '\\' || *c == '|' || *c == '?' || *c == '*' || (unsigned char)*c < 32;
        out += bad ? '_' : *c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? std::string("Prefab") : out;
}

bool editor_create_prefab(EditorContext& ctx, const std::vector<Entity>& p_tops)
{
    if (!ctx.world || !ctx.project || p_tops.empty() || ctx.prefab_tab) return false;
    World& world = *ctx.world;
    std::filesystem::path folder = ctx.project->assets_dir;
    if (ctx.world_tab) {
        if (const std::filesystem::path* world_path = ctx.world_tab->_path(ctx)) folder = world_path->parent_path();
    }
    const NameComponent* name = p_tops.size() == 1 ? world.try_get<NameComponent>(p_tops.front()) : nullptr;
    const std::string stem = _file_stem(name && name->name[0] ? name->name : "Prefab");
    std::error_code ec;
    std::filesystem::path path = folder / (stem + PrefabLibrary::EXTENSION);
    for (int i = 1; std::filesystem::exists(path, ec); ++i) path = folder / (stem + " " + std::to_string(i) + PrefabLibrary::EXTENSION);

    Guid prefab;
    Entity instance = ENTITY_NULL;
    if (prefab_create(world, p_tops, path, prefab, instance) != Error::OK) {
        log_write("Prefab: couldn't create '%s'.", path.string().c_str());
        return false;
    }
    if (ctx.assets) ctx.assets->request_rebuild();
    if (ctx.selection) ctx.selection->select(instance);
    log_write("Prefab: created '%s'.", path.filename().string().c_str());
    return true;
}

void editor_revert_prefab(EditorContext& ctx, Entity p_entity)
{
    if (!ctx.world) return;
    World& world = *ctx.world;
    Prefab* prefab = prefab_for(world, p_entity);
    if (!prefab) return;
    if (prefab_is_part(world, p_entity)) prefab_revert_entity(world, p_entity, *prefab);
    else prefab_revert_instance(world, p_entity, *prefab, true);
    if (ctx.selection) ctx.selection->prune(world);
}

void editor_unpack_prefab(EditorContext& ctx, Entity p_entity)
{
    if (!ctx.world) return;
    World& world = *ctx.world;
    const Entity root = prefab_root_of(world, p_entity);
    const EntityIdComponent* id = world.try_get<EntityIdComponent>(root);
    if (!id) return;
    const Guid instance = id->guid;
    std::vector<Entity> parts;
    world.view<PrefabPartComponent>([&](Entity e, const PrefabPartComponent& p_part) {
        if (p_part.instance == instance) parts.push_back(e);
    });
    for (const Entity e : parts) world.remove<PrefabPartComponent>(e);
    world.remove<PrefabInstanceComponent>(root);
}

void editor_open_prefab(EditorContext& ctx, Guid p_prefab)
{
    const std::filesystem::path path = prefab_library().path(p_prefab);
    if (path.empty() || !ctx.tabs) return;
    ctx.tabs->open<PrefabTab>(p_prefab)->path = path;
}

}
