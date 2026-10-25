
#pragma once
#include <core/application/application.h>
#include <editor/project_manager/project_manager.h>
#include <editor/tabs/tab_manager.h>
#include <editor/tabs/world/world_tab.h>
#include <editor/tabs/settings/editor_settings_tab.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/rendering/render_path/project_manager_render_path.h>
#include <editor/editor_settings.h>
#include <editor/viewport/editor_camera.h>
#include <editor/editor_resources.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/texture_viewer.h>
#include <editor/assets/thumbnail_cache.h>
#include <editor/tabs/world/scene/editor_folders.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <editor/play/play_session.h>
#include <drivers/git/git_driver.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen {

struct EditorApplication : Application
{
    struct SavedTab
    {
        TabKind kind = TabKind::WORLD;
        Guid guid;
    };

    struct SavedTabs
    {
        std::vector<SavedTab> tabs;
        uint32_t active = 0;
    };

    ProjectManager project_manager;
    Drawer drawer;
    WorldTab world_tab;
    TabManager tabs;

    EditorSettings settings;
    EditorResources resources;
    AssetImportTracker imports;
    AssetRegistry assets;
    EditorFolders folders;
    TextureViewer texture_viewer;
    ThumbnailCache thumbnails;

    EditorCamera editor_camera;
    EditorSelection selection;

    PlaySession play;
    PlaySession run;

    drivers::GitDriver git;
    std::filesystem::path reopen_root;

    Error on_init() override;
    void on_shutdown() override;
    void on_update(float p_dt) override;

    size_t imports_notified = 0;

    Error open_project(const std::filesystem::path& p_root);
    void close_project();
    void save_all();
    void reload_project(std::function<void()> p_while_closed);

    void _load_state();
    void _save_state();

    std::unordered_map<std::string, SavedTabs> _project_tabs;
    uint64_t _tabs_signature = 0;

    uint64_t _tabs_hash() const;
    void _capture_tabs();
    void _restore_tabs();

    struct TitlebarLayout {
        ImVec2 origin;
        float width  = 0;
        float menu_h = 0;
        float bar_h = 0;
        float tab_h = 0;
        float btn_w = 0;
        float logo = 0;
    };

    void _draw_titlebar();
    
    void _titlebar_menus(const TitlebarLayout& L);
    void _titlebar_caption_buttons(const TitlebarLayout& L);
    void _titlebar_tabs(const TitlebarLayout& L);
    void _titlebar_logo(const TitlebarLayout& L);
    void _titlebar_block(const TitlebarLayout& L, ImVec2 min, ImVec2 max);
    
    void _titlebar_help_menu();
    void _titlebar_editor_menu();

    EditorContext _make_context();

    bool wants_docking() const override { return true; }
    bool wants_custom_titlebar() const override { return false; }
    bool should_tick_game() const override { return false; }
    VsyncMode vsync_mode() const override { return project.loaded() ? settings.vsync_mode : VsyncMode::ON; }
    float fps_cap() const override { return (float)settings.fps_cap; }
    BackgroundPolicy background_policy() const override { return { .render_unfocused = false, .housekeeping_ms = 1000 }; }
    void on_background_tick() override;
    
    void update_camera(float p_dt) override;
    const Camera& active_camera() const override;
    const World& render_world() const override;
    
    RenderPath* create_render_path() override { return new ProjectManagerRenderPath(); }
};

}