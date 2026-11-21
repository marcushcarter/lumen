#pragma once
#include <core/application/application.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/rendering/render_path/project_manager_render_path.h>
#include <drivers/git/git_driver.h>
#include <drivers/toml/toml_helpers.h>
#include <editor/application/editor_context.h>
#include <editor/application/editor_resources.h>
#include <editor/application/editor_settings.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/assets/texture_viewer.h>
#include <editor/assets/import/import_dialog.h>
#include <editor/assets/references_dialog.h>
#include <editor/assets/thumbnail_cache.h>
#include <editor/play/play_session.h>
#include <editor/play/editor_play.h>
#include <editor/project_manager/project_manager.h>
#include <editor/documents/document_manager.h>
#include <editor/documents/scene/editor_selection.h>
#include <editor/workspace/workspace.h>
#include <editor/viewport/orbit_camera.h>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace lumen {

struct EditorApplication : Application
{
    static constexpr const char* FILE_EDITOR_STATE = "editor_state.cfg";
    static constexpr const char* FILE_PROJECT_STATE = "project_state.cfg";
    static constexpr const char* FILE_LOADING_MARKER = "project_loading.lock";

    EditorSettings settings;
    EditorResources resources;
    EditorContext _ctx;

    ProjectManager project_manager;
    AssetImportTracker imports;
    static constexpr float EDITOR_RENDER_SCALE = 0.75f;

    AssetRegistry assets;
    std::filesystem::path _bake_source;
    WaterWorld _water_preview;
    bool _water_preview_on = false;
    EditorSelection selection;
    TextureViewer texture_viewer;
    ImportDialog import_dialog;
    ReferencesDialog references_dialog;
    ThumbnailCache thumbnails;
    EditorPlay play;
    PlaySession run;
    drivers::GitDriver git;
    std::filesystem::path reopen_root;
    std::filesystem::path session_project;
    bool loading_marker = false;
    size_t imports_notified = 0;

    struct SavedDocument {
        DocumentKind kind = DocumentKind::MODEL;
        Guid guid;
    };

    Workspace workspace;
    DocumentManager documents;
    std::vector<SavedDocument> _saved_documents;
    Guid _saved_scene;
    uint64_t _documents_signature = 0;

    toml::table _project_state_default;

    OrbitCamera editor_camera;

    EditorContext& _context();

    Error on_init() override;
    void on_shutdown() override;
    void on_update(float p_dt) override;
    void on_background_tick() override;

    void _tick_imports();
    EditorDocument* _open_document(DocumentKind p_kind, Guid p_guid);
    void _reload_document(EditorDocument* p_document);
    Error open_project(const std::filesystem::path& p_root);
    bool close_project();
    bool _confirm_cancel_imports(const wchar_t* p_action);
    bool on_close_requested() override;
    void save_all();
    void reload_project(std::function<void()> p_while_closed);
    void _save_focused();

    uint64_t _documents_hash() const;
    void _restore_documents();

    void _load_state();
    void _queue_last_project(const std::filesystem::path& p_last);
    void _write_loading_marker(const std::filesystem::path& p_root);
    void _clear_loading_marker();
    void _save_state();
    void _load_project_state();
    void _save_project_state();
    toml::table _project_state_write();
    void _project_state_read(const toml::table& p_tbl);
    void _apply_theme();

    void _play_start();
    void _water_bake_start();
    void _water_bake_poll();
    void _water_preview_update(float p_dt);
    void _water_preview_stop();
    void _keep_editor_view();
    void on_simulate(float p_dt) override;
    Scene& simulated_scene() override { return play.active() ? play.runtime : scene; }
    void update_camera(float p_dt) override;
    const Camera& active_camera() const override;
    bool renders_scene() const override;
    const Scene& render_scene() const override;

    struct TitlebarLayout {
        ImVec2 origin;
        float width = 0;
        float height = 0;
        float btn_w = 0;
        float logo = 0;
    };

    void _titlebar_block(const TitlebarLayout& p_layout, ImVec2 p_min, ImVec2 p_max);
    void _titlebar_menus();
    void _titlebar_caption_buttons(const TitlebarLayout& p_layout);
    void _titlebar_logo(const TitlebarLayout& p_layout);
    void _titlebar_title(const TitlebarLayout& p_layout, float p_menus_end, float p_right);
    float _titlebar_search(const TitlebarLayout& p_layout, float p_right);
    void _draw_titlebar();

    bool wants_docking() const override { return false; }
    bool wants_custom_titlebar() const override { return false; }
    bool should_tick_game() const override { return false; }
    bool loads_authoring_assets() const override { return true; }
    VsyncMode vsync_mode() const override { return project.loaded() ? settings.vsync_mode : VsyncMode::ON; }
    float fps_cap() const override { return (float)settings.fps_cap; }
    BackgroundPolicy background_policy() const override { return { .render_unfocused = false, .housekeeping_ms = 1000 }; }
    RenderPath* create_render_path() override { return new ProjectManagerRenderPath(); }
};

}
