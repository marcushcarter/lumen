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
#include <editor/assets/thumbnail_cache.h>
#include <editor/panels/shared_panels.h>
#include <editor/play/play_session.h>
#include <editor/project_manager/project_manager.h>
#include <editor/tabs/settings/editor_settings_tab.h>
#include <editor/tabs/tab_manager.h>
#include <editor/tabs/world/scene/editor_folders.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <editor/tabs/world/world_tab.h>
#include <editor/viewport/editor_camera.h>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace lumen {

struct EditorApplication : Application
{
    /*******************/
    /**** LIFECYCLE ****/
    /*******************/

    static constexpr const char* FILE_EDITOR_STATE = "editor_state.cfg";
    static constexpr const char* FILE_PROJECT_STATE = "project_state.cfg";

    EditorSettings settings;
    EditorResources resources;
    EditorContext _ctx;

    EditorContext& _context();

    Error on_init() override;
    void on_shutdown() override;
    void on_update(float p_dt) override;
    void on_background_tick() override;

    bool wants_docking() const override { return true; }
    bool wants_custom_titlebar() const override { return false; }
    bool should_tick_game() const override { return false; }
    VsyncMode vsync_mode() const override { return project.loaded() ? settings.vsync_mode : VsyncMode::ON; }
    float fps_cap() const override { return (float)settings.fps_cap; }
    BackgroundPolicy background_policy() const override { return { .render_unfocused = false, .housekeeping_ms = 1000 }; }
    RenderPath* create_render_path() override { return new ProjectManagerRenderPath(); }

    /*****************/
    /**** PROJECT ****/
    /*****************/

    ProjectManager project_manager;
    AssetImportTracker imports;
    AssetRegistry assets;
    EditorFolders folders;
    EditorSelection selection;
    TextureViewer texture_viewer;
    ThumbnailCache thumbnails;
    PlaySession play;
    PlaySession run;
    drivers::GitDriver git;
    std::filesystem::path reopen_root;
    size_t imports_notified = 0;

    void _tick_imports();

    Error open_project(const std::filesystem::path& p_root);
    void close_project();
    void save_all();
    void reload_project(std::function<void()> p_while_closed);

    /**************/
    /**** TABS ****/
    /**************/

    struct SavedTab {
        TabKind kind = TabKind::WORLD;
        Guid guid;
    };

    struct SavedTabs {
        std::vector<SavedTab> tabs;
        uint32_t active = 0;
    };

    SharedPanels shared_panels;
    WorldTab world_tab;
    TabManager tabs;
    SavedTabs _saved_tabs;
    uint64_t _tabs_signature = 0;

    uint64_t _tabs_hash() const;
    void _restore_tabs();

    /***************/
    /**** STATE ****/
    /***************/

    toml::table _project_state_default;
    toml::table _project_state_legacy;

    void _load_state();
    void _save_state();
    void _load_project_state();
    void _save_project_state();
    toml::table _project_state_write();
    void _project_state_read(const toml::table& p_tbl);

    /****************/
    /**** CAMERA ****/
    /****************/

    EditorCamera editor_camera;

    void update_camera(float p_dt) override;
    const Camera& active_camera() const override;
    const World& render_world() const override;

    /******************/
    /**** TITLEBAR ****/
    /******************/

    struct TitlebarLayout {
        ImVec2 origin;
        float width = 0;
        float menu_h = 0;
        float bar_h = 0;
        float tab_h = 0;
        float btn_w = 0;
        float logo = 0;
    };

    void _titlebar_block(const TitlebarLayout& p_layout, ImVec2 p_min, ImVec2 p_max);
    void _titlebar_menus(const TitlebarLayout& p_layout);
    void _titlebar_caption_buttons(const TitlebarLayout& p_layout);
    void _titlebar_tabs(const TitlebarLayout& p_layout);
    void _titlebar_logo(const TitlebarLayout& p_layout);
    void _titlebar_editor_menu();

    void _draw_titlebar();
};

}
