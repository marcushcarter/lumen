#pragma once
#include <core/scene/template.h>
#include <core/scene/scene.h>
#include <editor/application/editor_context.h>
#include <editor/documents/scene/editor_selection.h>
#include <editor/viewport/orbit_camera.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

struct TemplateDocument
{
    static constexpr double DIRTY_POLL_S = 0.25;
    static constexpr uint32_t FRAME_ATTEMPTS = 240;

    Guid guid;
    std::filesystem::path path;
    Scene scene;
    EditorSelection selection;
    OrbitCamera camera;
    Guid root;
    uint64_t _hash = 0;
    uint64_t _saved_hash = 0;
    double _hash_poll = -1.0e9;
    bool framed = false;
    uint32_t _frame_attempts = 0;

    Error load();
    void unload();
    void reload();
    void tick();
    bool frame(EditorContext& ctx);
    Error save(EditorContext& ctx);
    void _fix_roots();
    bool dirty() const { return _hash != _saved_hash; }
    std::string name() const { return path.filename().string(); }

    static void template_changed(EditorContext& ctx, Guid p_template, std::unique_ptr<Template> p_old, const TemplateDocument* p_source);
};

Entity editor_spawn_template(EditorContext& ctx, Guid p_template, Entity p_parent, const vec3* p_position = nullptr);
bool editor_create_template(EditorContext& ctx, const std::vector<Entity>& p_tops);
void editor_revert_template(EditorContext& ctx, Entity p_entity);
void editor_unpack_template(EditorContext& ctx, Entity p_entity);
void editor_open_template(EditorContext& ctx, Guid p_template);

}
