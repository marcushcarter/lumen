#pragma once
#include <core/scene/scene.h>
#include <editor/layout/document_layout.h>
#include <editor/documents/editor_document.h>
#include <editor/assets/import/reimport_kind.h>
#include <editor/viewport/orbit_camera.h>
#include <editor/viewport/preview_viewport.h>
#include <filesystem>
#include <string>

namespace lumen {

struct AssetDocument : EditorDocument
{
    static constexpr double INFO_POLL_S = 1.0;

    std::filesystem::path path;
    DocumentLayout layout;
    PreviewViewport viewport;
    OrbitCamera orbit;
    Scene preview;
    Guid _preview_model;
    Entity _preview_entity;
    std::string _title;

    std::string source;
    ReimportKind reimport_kind = ReimportKind::NONE;
    uint64_t disk_size = 0;
    std::string _tooltip;
    std::filesystem::file_time_type _info_time{};
    double _info_poll = -1.0e9;

    void restore_view(const OrbitCamera& p_orbit);
    void _draw_lock(EditorContext& ctx, ImVec2 p_min, ImVec2 p_max);

    virtual const char* _type_name() const = 0;
    bool _resolve_path(EditorContext& ctx);
    void _refresh_info(EditorContext& ctx);
    void _create_layout();
    void _create_preview();
    void _update_orbit();
    void _refresh_title();
    void _draw_layout(EditorContext& ctx);
    void draw_import() const;

    const char* title() const override { return _title.c_str(); }
    const char* tooltip() const override { return _tooltip.empty() ? nullptr : _tooltip.c_str(); }
    const std::filesystem::path* asset_path() const override { return path.empty() ? nullptr : &path; }
    const Scene* preview_scene() const override { return &preview; }
    const Camera* preview_camera() const override { return &orbit.camera; }
    DocumentLayout* panels() override { return &layout; }
    bool locked(const EditorContext& ctx) const override;
    void destroy(EditorContext& ctx) override;
    void on_tick(EditorContext& ctx) override;
};

}
