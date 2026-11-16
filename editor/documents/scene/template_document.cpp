#include <editor/documents/scene/template_document.h>
#include <core/rendering/renderer.h>
#include <core/scene/components.h>
#include <core/scene/scene_file.h>
#include <editor/documents/scene/editor_spawn.h>
#include <editor/documents/scene/scene_document.h>
#include <editor/assets/asset_registry.h>
#include <core/project/project.h>
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <string>

namespace lumen {

Error TemplateDocument::load()
{
    using enum Error;
    scene.initialize();
    scene.load();
    selection.clear();
    SceneFile file;
    if (SceneFile::read(path, scene, true, file) != OK || file.type != AssetType::TEMPLATE || file.guid != guid) {
        log_write("Template: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    root = file.root;
    _fix_roots();
    _hash = _saved_hash = SceneFile::hash(scene);
    framed = false;
    _frame_attempts = 0;
    return OK;
}

void TemplateDocument::unload()
{
    selection.clear();
    scene.shutdown();
}

void TemplateDocument::reload()
{
    scene.unload();
    selection.clear();
    SceneFile file;
    if (SceneFile::read(path, scene, true, file) != Error::OK || file.type != AssetType::TEMPLATE) {
        log_write("Template: failed to reload '%s'.", path.string().c_str());
        return;
    }
    root = file.root;
    _fix_roots();
    _hash = _saved_hash = SceneFile::hash(scene);
}

void TemplateDocument::_fix_roots()
{
    std::vector<Entity> roots;
    scene.view<EntityIdComponent>([&](Entity p_entity, const EntityIdComponent&) {
        if (scene.parent(p_entity) == ENTITY_NULL) roots.push_back(p_entity);
    });
    std::sort(roots.begin(), roots.end(), [](Entity a, Entity b) { return a.index < b.index; });
    Entity main = scene.find(root);
    if (main == ENTITY_NULL || scene.parent(main) != ENTITY_NULL) main = roots.empty() ? ENTITY_NULL : roots.front();
    root = main != ENTITY_NULL ? scene.try_get<EntityIdComponent>(main)->guid : Guid{};
    for (const Entity e : roots) {
        if (e != main) scene.set_parent(e, main);
    }
}

void TemplateDocument::tick()
{
    scene.deferred_flush();
    selection.prune(scene);
    const double now = ImGui::GetTime();
    if (now - _hash_poll < DIRTY_POLL_S) return;
    _hash_poll = now;
    _hash = SceneFile::hash(scene);
}

Error TemplateDocument::save(EditorContext& ctx)
{
    using enum Error;
    scene.deferred_flush();
    _fix_roots();
    SceneFile file;
    file.guid = guid;
    file.root = root;
    file.type = AssetType::TEMPLATE;
    if (SceneFile::write(path, scene, file) != OK) {
        log_write("Template: failed to save '%s'.", path.string().c_str());
        return FAILED;
    }
    _hash = _saved_hash = SceneFile::hash(scene);
    template_changed(ctx, guid, template_library().take(guid), this);
    return OK;
}

void TemplateDocument::template_changed(EditorContext& ctx, Guid p_template, std::unique_ptr<Template> p_old, const TemplateDocument* p_source)
{
    Template* fresh = template_library().get(p_template);
    if (ctx.main_scene) template_refresh(*ctx.main_scene, p_template, p_old.get(), fresh, true);
    if (!ctx.scene_document) return;
    for (auto& doc : ctx.scene_document->templates) {
        if (doc.get() == p_source || doc->guid != p_template) continue;
        if (doc->dirty()) log_write("Template: '%s' changed while it has unsaved edits; save or reopen it to pick it up.", doc->path.filename().string().c_str());
        else doc->reload();
    }
}

bool TemplateDocument::frame(EditorContext& ctx)
{
    if (framed || !ctx.renderer) return false;
    if (++_frame_attempts > FRAME_ATTEMPTS) {
        framed = true;
        return false;
    }
    vec3 lo(FLT_MAX);
    vec3 hi(-FLT_MAX);
    bool any = false;
    bool pending = false;
    scene.view<ModelComponent>([&](Entity p_entity, const ModelComponent& p_model) {
        const ModelCache::Entry* entry = ctx.renderer->models.get(p_model.model);
        if (!entry || !entry->mesh.valid()) return;
        const LMesh* mesh = ctx.renderer->geometry.get(ctx.renderer->geometry.find(entry->mesh));
        if (!mesh) {
            pending = true;
            return;
        }
        const mat4 m = scene.world_matrix(p_entity);
        const float s = std::max(length(vec3(m[0])), std::max(length(vec3(m[1])), length(vec3(m[2]))));
        const vec3 c = vec3(m * vec4(vec3(mesh->bounds_sphere), 1.0f));
        const vec3 r(mesh->bounds_sphere.w * s);
        lo = min(lo, c - r);
        hi = max(hi, c + r);
        any = true;
    });
    if (pending) return false;
    framed = true;
    if (!any) return false;
    const vec3 center = (lo + hi) * 0.5f;
    const float radius = std::max(length(hi - lo) * 0.5f, 0.1f);
    const vec3 dir = normalize(vec3(0.6f, 0.45f, 0.65f));
    Camera cam;
    cam.position = center + dir * (radius * 2.5f);
    cam.rotation = glm::quatLookAt(-dir, vec3(0.0f, 1.0f, 0.0f));
    camera.place(cam);
    camera.speed = std::clamp(radius * 2.0f, EditorCamera::MIN_SPEED, EditorCamera::MAX_SPEED);
    return true;
}

/*****************/
/**** ACTIONS ****/
/*****************/

Entity editor_spawn_template(EditorContext& ctx, Guid p_template, Entity p_parent, const vec3* p_position)
{
    Template* tmpl = template_library().get(p_template);
    if (!tmpl || !ctx.scene || !ctx.renderer) return ENTITY_NULL;
    Scene& scene = *ctx.scene;
    const Camera& cam = ctx.renderer->main_view.camera;
    TransformComponent xf;
    xf.position = p_position ? *p_position : cam.position + cam.forward() * SPAWN_DISTANCE;
    const Entity e = template_spawn(scene, *tmpl, ENTITY_NULL, xf);
    if (e == ENTITY_NULL) return e;
    if (scene.valid(p_parent)) {
        scene.set_parent(e, p_parent);
        if (!scene.has<EditorExpandedTag>(p_parent)) scene.add<EditorExpandedTag>(p_parent);
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
    return out.empty() ? std::string("Template") : out;
}

bool editor_create_template(EditorContext& ctx, const std::vector<Entity>& p_tops)
{
    if (!ctx.scene || !ctx.project || p_tops.empty() || ctx.template_doc) return false;
    Scene& scene = *ctx.scene;
    std::filesystem::path folder = ctx.project->assets_dir;
    if (ctx.scene_document) {
        if (const std::filesystem::path* scene_path = ctx.scene_document->_path(ctx)) folder = scene_path->parent_path();
    }
    const NameComponent* name = p_tops.size() == 1 ? scene.try_get<NameComponent>(p_tops.front()) : nullptr;
    const std::string stem = _file_stem(name && name->name[0] ? name->name : "Template");
    std::error_code ec;
    std::filesystem::path path = folder / (stem + TemplateLibrary::EXTENSION);
    for (int i = 1; std::filesystem::exists(path, ec); ++i) path = folder / (stem + " " + std::to_string(i) + TemplateLibrary::EXTENSION);

    Guid tmpl;
    Entity instance = ENTITY_NULL;
    if (template_create(scene, p_tops, path, tmpl, instance) != Error::OK) {
        log_write("Template: couldn't create '%s'.", path.string().c_str());
        return false;
    }
    if (ctx.assets) ctx.assets->request_rebuild();
    if (ctx.selection) ctx.selection->select(instance);
    log_write("Template: created '%s'.", path.filename().string().c_str());
    return true;
}

void editor_revert_template(EditorContext& ctx, Entity p_entity)
{
    if (!ctx.scene) return;
    Scene& scene = *ctx.scene;
    Template* tmpl = template_for(scene, p_entity);
    if (!tmpl) return;
    if (template_is_part(scene, p_entity)) template_revert_entity(scene, p_entity, *tmpl);
    else template_revert_instance(scene, p_entity, *tmpl, true);
    if (ctx.selection) ctx.selection->prune(scene);
}

void editor_unpack_template(EditorContext& ctx, Entity p_entity)
{
    if (!ctx.scene) return;
    Scene& scene = *ctx.scene;
    const Entity root = template_root_of(scene, p_entity);
    const EntityIdComponent* id = scene.try_get<EntityIdComponent>(root);
    if (!id) return;
    const Guid instance = id->guid;
    std::vector<Entity> parts;
    scene.view<TemplatePartComponent>([&](Entity e, const TemplatePartComponent& p_part) {
        if (p_part.instance == instance) parts.push_back(e);
    });
    for (const Entity e : parts) scene.remove<TemplatePartComponent>(e);
    scene.remove<TemplateInstanceComponent>(root);
}

void editor_open_template(EditorContext& ctx, Guid p_template)
{
    const std::filesystem::path path = template_library().path(p_template);
    if (path.empty() || !ctx.scene_document) return;
    ctx.scene_document->open_template(ctx, p_template, path);
}

}
