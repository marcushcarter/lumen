#pragma once
#include <core/base/error.h>
#include <editor/application/editor_context.h>
#include <editor/documents/scene/properties_panel.h>
#include <editor/documents/scene/scene_panel.h>
#include <editor/documents/scene/template_document.h>
#include <editor/viewport/orbit_camera.h>
#include <editor/viewport/viewport.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

struct SceneDocument
{
    static constexpr int32_t SCENE = -1;
    static constexpr double DIRTY_POLL_S = 0.25;

    bool focus_request = false;

    Viewport viewport;
    ScenePanel scene_panel;
    PropertiesPanel properties;

    Guid guid;
    std::string _name;
    Guid _pending;
    bool _has_pending = false;
    uint64_t _hash = 0;
    uint64_t _saved_hash = 0;
    double _hash_poll = -1.0e9;

    std::vector<std::unique_ptr<TemplateDocument>> templates;
    std::vector<std::pair<uint32_t, std::unique_ptr<TemplateDocument>>> _closing;
    int32_t active = SCENE;
    OrbitCamera scene_camera;

    const std::filesystem::path* _path(EditorContext& ctx) const;
    bool _confirm_discard(EditorContext& ctx, int32_t p_index, const wchar_t* p_body, const wchar_t* p_save);
    Error _save_scene(EditorContext& ctx);
    static constexpr float PLAYER_EYE_HEIGHT = 1.2f;
    static constexpr float PLAYER_DISTANCE = 4.0f;
    static constexpr float PLAYER_RISE = 1.0f;
    static constexpr vec3 DEFAULT_CAMERA = vec3(0.0f, 2.0f, 5.0f);

    void _load(EditorContext& ctx, Guid p_scene);
    void _place_camera(EditorContext& ctx);
    void _unload(EditorContext& ctx);
    void _close_all(EditorContext& ctx);
    void _draw_empty(ImVec2 p_min, ImVec2 p_max);

    void shutdown(EditorContext& ctx);
    void open(Guid p_scene);
    void reset(EditorContext& ctx);
    bool has_world() const { return guid.valid(); }
    bool can_play() const { return has_world() && !active_template(); }
    Error write(EditorContext& ctx, const std::filesystem::path& p_path) const;

    TemplateDocument* active_template() const { return active >= 0 && active < (int32_t)templates.size() ? templates[active].get() : nullptr; }
    void open_template(EditorContext& ctx, Guid p_guid, const std::filesystem::path& p_path);
    void close_template(EditorContext& ctx, int32_t p_index, bool p_force);
    void activate(EditorContext& ctx, int32_t p_index);
    void bind(EditorContext& ctx);

    std::string document_name() const;
    bool scene_dirty() const { return has_world() && _hash != _saved_hash; }
    bool dirty() const;
    bool any_dirty() const;
    bool confirm_discard_all(EditorContext& ctx);
    Error save(EditorContext& ctx);
    void save_all(EditorContext& ctx);

    void tick(EditorContext& ctx);
    void draw_view(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);
    void end_frame();
};

}
