#include <editor/documents/scene/scene_document.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/scene/scene_file.h>
#include <core/scene/components.h>
#include <core/rendering/renderer.h>
#include <algorithm>
#include <cmath>
#include <drivers/windows/dialogs_win32.h>
#include <drivers/windows/window_driver_win32.h>
#include <editor/assets/asset_registry.h>
#include <editor/documents/document_manager.h>
#include <editor/documents/scene/editor_selection.h>
#include <editor/play/editor_play.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>

namespace lumen {

void SceneDocument::_close_all(EditorContext& ctx)
{
    for (auto& c : _closing) c.second->unload();
    _closing.clear();
    viewport.cursor_lock.end();
    viewport.camera_drag.release();
    for (auto& t : templates) t->unload();
    templates.clear();
    active = SCENE;
    bind(ctx);
}

void SceneDocument::shutdown(EditorContext& ctx)
{
    _close_all(ctx);
    scene_panel.shutdown();
    properties.shutdown();
}

void SceneDocument::open(Guid p_scene)
{
    _pending = p_scene;
    _has_pending = true;
}

void SceneDocument::reset(EditorContext& ctx)
{
    _close_all(ctx);
    if (has_world()) _unload(ctx);
    guid = Guid{};
    _pending = Guid{};
    _has_pending = false;
    _hash = 0;
    _saved_hash = 0;
    _name.clear();
}

const std::filesystem::path* SceneDocument::_path(EditorContext& ctx) const
{
    const AssetRegistry::Entry* e = ctx.assets ? ctx.assets->find(guid) : nullptr;
    return e && e->type == AssetType::SCENE ? &e->path : nullptr;
}

bool SceneDocument::_confirm_discard(EditorContext& ctx, int32_t p_index, const wchar_t* p_body, const wchar_t* p_save)
{
    using Choice = drivers::Win32Dialogs::SaveChoice;
    const std::string name = p_index == SCENE ? _name : templates[p_index]->name();
    const std::wstring heading = L"'" + drivers::WindowDriverWin32::utf8_to_wide(name) + L"' has unsaved changes";
    const Choice choice = drivers::Win32Dialogs::ask_save(L"Lumen Editor", heading.c_str(), p_body, p_save);
    if (choice == Choice::CANCEL) return false;
    if (choice == Choice::DISCARD) return true;
    return p_index == SCENE ? _save_scene(ctx) == Error::OK : templates[p_index]->save(ctx) == Error::OK;
}

Error SceneDocument::_save_scene(EditorContext& ctx)
{
    const int32_t was = active;
    active = SCENE;
    const Error err = save(ctx);
    active = was;
    return err;
}

void SceneDocument::_unload(EditorContext& ctx)
{
    ctx.main_scene->unload();
    ctx.main_selection->clear();
    guid = Guid{};
    _hash = _saved_hash = 0;
}

void SceneDocument::_load(EditorContext& ctx, Guid p_scene)
{
    const AssetRegistry::Entry* e = ctx.assets->find(p_scene);
    if (!e || e->type != AssetType::SCENE) {
        log_write("Scene: no scene with guid %s.", p_scene.to_string().c_str());
        return;
    }
    if (active != SCENE) activate(ctx, SCENE);
    _unload(ctx);
    SceneFile file;
    if (SceneFile::read(e->path, *ctx.main_scene, true, file) != Error::OK) {
        log_write("Scene: failed to load '%s'.", e->path.string().c_str());
        ctx.main_scene->unload();
        return;
    }
    guid = p_scene;
    _hash = _saved_hash = SceneFile::hash(*ctx.main_scene);
    _hash_poll = ImGui::GetTime();
    _place_camera(ctx);
}

void SceneDocument::_place_camera(EditorContext& ctx)
{
    OrbitCamera* cam = ctx.editor_camera;
    if (!cam) return;
    Scene& world = *ctx.main_scene;
    Entity player = ENTITY_NULL;
    world.view<CharacterComponent, TransformComponent>([&](Entity p_entity, const CharacterComponent& p_character, const TransformComponent&) {
        if (player == ENTITY_NULL && p_character.controller == CharacterController::PLAYER) player = p_entity;
    });

    vec3 target(0.0f);
    vec3 position = DEFAULT_CAMERA;
    if (player != ENTITY_NULL) {
        const mat4 m = world.world_matrix(player);
        target = vec3(m[3]) + vec3(0.0f, PLAYER_EYE_HEIGHT, 0.0f);
        vec3 facing = vec3(m * vec4(0.0f, 0.0f, 1.0f, 0.0f));
        facing.y = 0.0f;
        facing = dot(facing, facing) > 1e-6f ? normalize(facing) : vec3(0.0f, 0.0f, 1.0f);
        position = target - facing * PLAYER_DISTANCE + vec3(0.0f, PLAYER_RISE, 0.0f);
    }
    cam->look_from(position, target);
    scene_camera = *cam;
    if (ctx.renderer) ctx.renderer->camera_cut();
}

Error SceneDocument::write(EditorContext& ctx, const std::filesystem::path& p_path) const
{
    SceneFile file;
    file.guid = guid;
    return SceneFile::write(p_path, *ctx.main_scene, file);
}

void SceneDocument::bind(EditorContext& ctx)
{
    if (TemplateDocument* doc = active_template()) {
        ctx.scene = &doc->scene;
        ctx.selection = &doc->selection;
        ctx.template_doc = doc;
        return;
    }
    ctx.scene = ctx.main_scene;
    ctx.selection = ctx.main_selection;
    ctx.template_doc = nullptr;
}

void SceneDocument::activate(EditorContext& ctx, int32_t p_index)
{
    if (p_index >= (int32_t)templates.size()) p_index = SCENE;
    if (p_index == active) return;
    if (p_index != SCENE && ctx.play && ctx.play->active() && ctx.pie_toggle_play) ctx.pie_toggle_play();
    OrbitCamera* cam = ctx.editor_camera;
    if (cam) {
        if (TemplateDocument* out = active_template()) out->camera = *cam;
        else scene_camera = *cam;
    }
    active = p_index;
    bind(ctx);
    if (cam) {
        if (TemplateDocument* in = active_template()) *cam = in->camera;
        else *cam = scene_camera;
    }
    viewport.gizmo.reset();
    if (ctx.render_path) ctx.render_path->outline.source_version = UINT64_MAX;
}

void SceneDocument::open_template(EditorContext& ctx, Guid p_guid, const std::filesystem::path& p_path)
{
    for (int32_t i = 0; i < (int32_t)templates.size(); i++) {
        if (templates[i]->guid != p_guid) continue;
        activate(ctx, i);
        return;
    }
    auto doc = std::make_unique<TemplateDocument>();
    doc->guid = p_guid;
    doc->path = p_path;
    if (ctx.editor_camera) doc->camera = *ctx.editor_camera;
    if (doc->load() != Error::OK) {
        doc->unload();
        return;
    }
    templates.push_back(std::move(doc));
    activate(ctx, (int32_t)templates.size() - 1);
}

void SceneDocument::close_template(EditorContext& ctx, int32_t p_index, bool p_force)
{
    if (p_index < 0 || p_index >= (int32_t)templates.size()) return;
    TemplateDocument& doc = *templates[p_index];
    if (!p_force && doc.dirty() && !_confirm_discard(ctx, p_index, L"Do you want to save them before closing?", L"Save and Close")) return;
    const int32_t was = active;
    if (was == p_index) activate(ctx, SCENE);
    _closing.emplace_back(0u, std::move(templates[p_index]));
    templates.erase(templates.begin() + p_index);
    if (was > p_index) active = was - 1;
    bind(ctx);
}

std::string SceneDocument::document_name() const
{
    if (const TemplateDocument* doc = active_template()) return doc->name();
    return _name;
}

bool SceneDocument::dirty() const
{
    if (const TemplateDocument* doc = active_template()) return doc->dirty();
    return scene_dirty();
}

bool SceneDocument::any_dirty() const
{
    if (scene_dirty()) return true;
    for (const auto& t : templates) if (t->dirty()) return true;
    return false;
}

bool SceneDocument::confirm_discard_all(EditorContext& ctx)
{
    if (scene_dirty() && !_confirm_discard(ctx, SCENE, L"Do you want to save them before closing?", L"Save and Close")) return false;
    for (int32_t i = 0; i < (int32_t)templates.size(); i++) {
        if (templates[i]->dirty() && !_confirm_discard(ctx, i, L"Do you want to save them before closing?", L"Save and Close")) return false;
    }
    return true;
}

Error SceneDocument::save(EditorContext& ctx)
{
    using enum Error;
    if (TemplateDocument* doc = active_template()) return doc->save(ctx);
    if (!has_world()) return OK;
    const std::filesystem::path* path = _path(ctx);
    if (!path) return FAILED;
    if (write(ctx, *path) != OK) {
        log_write("Scene: failed to save '%s'.", path->string().c_str());
        return FAILED;
    }
    _hash = _saved_hash = SceneFile::hash(*ctx.main_scene);
    return OK;
}

void SceneDocument::save_all(EditorContext& ctx)
{
    for (auto& t : templates) {
        if (t->dirty()) t->save(ctx);
    }
    if (scene_dirty()) _save_scene(ctx);
}

void SceneDocument::tick(EditorContext& ctx)
{
    for (size_t i = 0; i < _closing.size();) {
        if (++_closing[i].first < DocumentManager::DESTROY_DELAY_FRAMES) { ++i; continue; }
        _closing[i].second->unload();
        _closing.erase(_closing.begin() + (std::ptrdiff_t)i);
    }
    for (int32_t i = 0; i < (int32_t)templates.size();) {
        TemplateDocument& doc = *templates[i];
        if (!ctx.assets->dirty && !ctx.assets->find(doc.guid)) {
            close_template(ctx, i, true);
            continue;
        }
        doc.tick();
        ++i;
    }
    if (TemplateDocument* doc = active_template()) {
        if (doc->frame(ctx) && ctx.editor_camera) *ctx.editor_camera = doc->camera;
    }

    if (!ctx.assets || ctx.assets->dirty) return;
    if (_has_pending) {
        _has_pending = false;
        if (_pending == guid) activate(ctx, SCENE);
        else if (!scene_dirty() || _confirm_discard(ctx, SCENE, L"Do you want to save them before opening another scene?", L"Save and Continue")) _load(ctx, _pending);
    }
    if (has_world() && !_path(ctx)) _unload(ctx);

    const double now = ImGui::GetTime();
    if (has_world() && now - _hash_poll >= DIRTY_POLL_S) {
        _hash_poll = now;
        _hash = SceneFile::hash(*ctx.main_scene);
    }

    const AssetRegistry::Entry* e = has_world() ? ctx.assets->find(guid) : nullptr;
    _name = e ? e->path.filename().string() : std::string();
}

void SceneDocument::_draw_empty(ImVec2 p_min, ImVec2 p_max)
{
    const UiStyle& ui = ui_style();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p_min, p_max, ui.color.viewport_back);
    ui_text_centered(dl, p_min, p_max, ui.color.text_dim, "Open a scene from the content browser");
}

void SceneDocument::draw_view(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max)
{
    viewport.play = ctx.play;
    if (!has_world() && !active_template()) {
        _draw_empty(p_min, p_max);
        return;
    }
    viewport.draw(ctx, p_min, p_max);
}

void SceneDocument::end_frame()
{
    if (TemplateDocument* doc = active_template()) doc->scene.deferred_flush();
}

}
