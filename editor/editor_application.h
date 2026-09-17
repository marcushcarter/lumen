
#pragma once
#include <core/application/application.h>
#include <editor/docking/editor.h>
#include <editor/project_manager/project_manager.h>
#include <editor/editor_settings.h>
#include <editor/editor_resources.h>
#include <editor/editor_camera.h>
#include <editor/assets/asset_import_tracker.h>
#include <editor/popup/popup_manager.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/rendering/render_path/project_manager_render_path.h>
#include <vector>

namespace lumen {

struct EditorApplication : Application
{
    enum class EditorMode { Edit, Play } mode = EditorMode::Edit;

    ProjectManager project_manager;
    Editor editor;
    PopupManager popups;

    EditorSettings settings;
    EditorResources resources;
    AssetImportTracker imports;

    int active_tab = 0;
    int pending_tab = -1;
    EditorCamera editor_camera;

    Error on_init() override;
    void on_shutdown() override;
    void on_update(float p_dt) override;

    Error open_project(const std::filesystem::path& p_root);
    void close_project();

    void _load_state();
    void _save_state();

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
    bool should_tick_game() const override { return mode == EditorMode::Play && !paused; }
    
    void update_camera(float p_dt) override;
    const Camera& active_camera() const override;
    
    RenderPath* create_render_path() override { return new ProjectManagerRenderPath(); }
};

}