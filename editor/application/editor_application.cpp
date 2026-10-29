#include <editor/application/editor_application.h>
#include <core/io/embedded_resource.h>
#include <core/io/file_io.h>
#include <core/base/hash.h>
#include <core/io/path.h>
#include <core/version.h>
#include <editor/assets/content_cooker.h>
#include <editor/assets/import/icon_cooker.h>
#include <editor/export/project_exporter.h>
#include <editor/tabs/material/material_tab.h>
#include <editor/tabs/model/model_tab.h>
#include <editor/tabs/world/panels/project_settings_panel.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace lumen {

Error EditorApplication::on_init()
{
    using enum Error;
    Error err;

    ui_style().build();
    ui_style().apply_to_imgui();

    win32.window_set_custom_titlebar(true);
    win32.window_set_title("Lumen Editor");

    err = resources.initialize(dd);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = win32.window_set_icon(EmbeddedResource::load_icon(L"LUMEN_ICON"));
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    ImGuiIO& io = ImGui::GetIO();
    {
        EmbeddedResource::Blob jb = EmbeddedResource::load(L"FONTS_JETBRAINS_MONO_REGULAR_TTF");
        ImFontConfig jb_cfg;
        jb_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)jb.data, (int)jb.size, ui_style().font.size, &jb_cfg);

        EmbeddedResource::Blob fa = EmbeddedResource::load(L"FONTS_FA_SOLID_900_OTF");
        static const ImWchar fa_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        ImFontConfig fa_cfg;
        fa_cfg.MergeMode = true;
        fa_cfg.PixelSnapH = true;
        fa_cfg.GlyphOffset.y = ui_style().font.icon_offset_y;
        fa_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)fa.data, (int)fa.size, ui_style().font.size, &fa_cfg, fa_ranges);
        io.Fonts->Build();
    }

    err = project_manager.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    shared_panels.initialize();
    git.log_callback = [](const std::string& p_line) { log_write("[Git] %s", p_line.c_str()); };
    err = world_tab.initialize(shared_panels);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = play.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = run.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = texture_viewer.initialize(dd);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = thumbnails.initialize(dd);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    _ctx.win32 = &win32;
    _ctx.imgui = &imgui;
    _ctx.git = &git;
    _ctx.renderer = &renderer;
    _ctx.project = &project;
    _ctx.world = &world;
    _ctx.project_manager = &project_manager;
    _ctx.world_tab = &world_tab;
    _ctx.shared_panels = &shared_panels;
    _ctx.tabs = &tabs;
    _ctx.selection = &selection;
    _ctx.settings = &settings;
    _ctx.profiling = &profiling;
    _ctx.resources = &resources;
    _ctx.imports = &imports;
    _ctx.assets = &assets;
    _ctx.folders = &folders;
    _ctx.texture_viewer = &texture_viewer;
    _ctx.thumbnails = &thumbnails;
    _ctx.open_project_callback = [this](const std::filesystem::path& p_root) { open_project(p_root); };
    _ctx.close_project_callback = [this] { close_project(); };
    _ctx.save_all_callback = [this] { save_all(); };
    _ctx.reload_project_callback = [this](std::function<void()> p_op) { reload_project(std::move(p_op)); };
    _ctx.pie_is_playing = [this] { return play.is_running(); };
    _ctx.pie_toggle_play = [this] {
        if (play.is_running()) { play.stop(); return; }
        ContentCooker::cook(project);
        play.launch(project.root);
    };
    _ctx.run_is_running = [this](const std::filesystem::path& p_root) { return run.is_running_for(p_root); };
    _ctx.run_toggle = [this](const std::filesystem::path& p_root) {
        if (run.is_running_for(p_root)) { run.stop(); return; }
        ContentCooker::cook_root(p_root);
        run.relaunch(p_root);
    };

    git.ignore_lines = { std::string("/") + Project::DIR_EDITOR + "/" };
    _project_state_default = _project_state_write();
    _load_state();
    settings.theme.apply();

    if (win32.window_set_titlebar_color((COLORREF)Theme::to_colorref(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg])) != OK) log_write("Editor: custom titlebar color unsupported (needs Windows 11).");

    return OK;
}

void EditorApplication::on_shutdown()
{
    EditorContext& ctx = _context();
    _save_project_state();
    tabs.clear(ctx);
    texture_viewer.shutdown();
    thumbnails.shutdown(ctx);

    play.shutdown();
    run.shutdown();
    git.shutdown();
    resources.shutdown();

    _save_state();
    project_manager.save_recents();

    project_manager.shutdown();
    world_tab.shutdown();
    shared_panels.shutdown();
}

void EditorApplication::on_update(float p_dt)
{
    play.poll();
    run.poll();
    _draw_titlebar();

    EditorContext& ctx = _context();
    if (project.loaded()) {
        _tick_imports();
        assets.tick();
        selection.prune(world, folders);

        tabs.update(ctx, p_dt);
        texture_viewer.draw(ctx);
        thumbnails.update(ctx);
        if (tabs.active && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) tabs.active->save(ctx);

        const uint64_t signature = _tabs_hash();
        if (signature != _tabs_signature) {
            _tabs_signature = signature;
            settings.dirty = true;
        }
    } else if (!reopen_root.empty() && !pending_render_path) {
        const std::filesystem::path root = std::move(reopen_root);
        reopen_root.clear();
        open_project(root);
    } else {
        project_manager.on_update(ctx);
    }

    if (settings.dirty && !ImGui::IsAnyItemActive()) {
        _save_state();
        _save_project_state();
        settings.dirty = false;
    }
}

void EditorApplication::on_background_tick()
{
    play.poll();
    run.poll();
    if (!project.loaded()) return;

    imports.tick();
    std::vector<std::string> finished;
    for (size_t i = imports_notified; i < imports.completed.size(); i++) {
        if (imports.completed[i].notify) finished.push_back(imports.completed[i].dst.stem().string());
    }
    imports_notified = imports.completed.size();
    if (finished.empty()) return;
    const std::string message = finished.size() == 1 ? finished.front() + " finished importing." : std::to_string(finished.size()) + " assets finished importing.";
    win32.notify("Import complete", message);
    win32.window_flash();
}

void EditorApplication::_tick_imports()
{
    imports.tick();
    if (!imports.completed.empty()) assets.request_rebuild();
    for (const auto& c : imports.completed) {
        if (c.dst.extension() == ".lmodel") {
            std::error_code mat_ec;
            for (auto it = std::filesystem::directory_iterator(c.dst.parent_path(), mat_ec); !mat_ec && it != std::filesystem::directory_iterator(); it.increment(mat_ec)) {
                if (it->path().extension() == ".lmaterial") renderer.materials.load(it->path());
            }
            renderer.models.load(c.dst);
            continue;
        }
        LAssetHeader ah{};
        if (!read_asset_header(c.content_bin, ah)) continue;
        switch (ah.type) {
            case AssetType::TEXTURE:
                if (const LTexture* old = renderer.textures.get(c.guid)) imgui.texture_cache.retire(old->image.image_view, renderer.frame_number, renderer.frame_count);
                renderer.textures.unload(c.guid);
                renderer.textures.load(c.guid, c.content_bin);
                if (c.guid == project.settings.window_icon) IconCooker::cook_window_icon(project);
                break;
            case AssetType::MESH:
                renderer.geometry.unload(c.guid);
                renderer.geometry.load(c.guid, c.content_bin);
                break;
            default:
                break;
        }
    }
    imports.completed.clear();
    imports_notified = 0;
}

Error EditorApplication::open_project(const std::filesystem::path& p_root)
{
    using enum Error;

    Error err = project_load(p_root);
    if (err != OK) project_unload();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    assets.open(project.assets_dir);
    folders.clear();
    selection.clear();
    render_path_request(new EditorRenderPath());
    project_manager.add_recent(project.root, project.name);
    if (!run.is_running_for(p_root)) {
        run.pending_root.clear();
        run.stop();
    }
    EditorContext& ctx = _context();
    tabs.clear(ctx);
    tabs.add(&world_tab, WorldTab::KIND);
    tabs.focus(&world_tab);
    _load_project_state();
    _restore_tabs();
    _tabs_signature = _tabs_hash();
    git.initialize(project.root);

    // the icon texture may have been reimported or the .ico deleted since the last session
    if (project.settings.window_icon.valid() && IconCooker::stale(project.content_path(project.settings.window_icon), project.window_icon_path())) {
        IconCooker::cook_window_icon(project);
    }

    return OK;
}

void EditorApplication::close_project()
{
    play.stop();
    pending_transition = [this]{
        EditorContext& ctx = _context();
        _save_project_state();
        _save_state();
        texture_viewer.close(ctx);
        tabs.clear(ctx);
        thumbnails.close(ctx);
        assets.close();
        folders.clear();
        selection.clear();
        project_unload();
    };
    render_path_request(new ProjectManagerRenderPath());
}

void EditorApplication::save_all()
{
    EditorContext& ctx = _context();
    for (EditorTab* t : tabs.tabs) {
        if (t->dirty()) t->save(ctx);
    }
    project.save();
}

void EditorApplication::reload_project(std::function<void()> p_while_closed)
{
    if (!project.loaded() || !reopen_root.empty()) return;
    reopen_root = project.root;
    close_project();
    std::function<void()> close = std::move(pending_transition);
    pending_transition = [close = std::move(close), op = std::move(p_while_closed)] {
        close();
        if (op) op();
    };
}

static const char* _tab_kind_name(TabKind p_kind)
{
    switch (p_kind) {
        case TabKind::WORLD: return "world";
        case TabKind::EDITOR_SETTINGS: return "editor_settings";
        case TabKind::MODEL: return "model";
        case TabKind::MATERIAL: return "material";
    }
    return "world";
}

static bool _tab_kind_from_string(const std::string& p_name, TabKind& r_kind)
{
    if (p_name == "world") r_kind = TabKind::WORLD;
    else if (p_name == "editor_settings") r_kind = TabKind::EDITOR_SETTINGS;
    else if (p_name == "model") r_kind = TabKind::MODEL;
    else if (p_name == "material") r_kind = TabKind::MATERIAL;
    else return false;
    return true;
}

uint64_t EditorApplication::_tabs_hash() const
{
    uint64_t h = HASH_FNV_OFFSET;
    for (const EditorTab* t : tabs.tabs) {
        h = hash_fnv_step(h, (uint64_t)t->kind);
        h = hash_fnv_step(h, t->guid.value);
        h = hash_fnv_step(h, t == tabs.active ? 1u : 0u);
    }
    return h;
}

void EditorApplication::_restore_tabs()
{
    const SavedTabs& saved = _saved_tabs;

    EditorTab* active = &world_tab;
    for (uint32_t i = 0; i < (uint32_t)saved.tabs.size(); i++) {
        const SavedTab& t = saved.tabs[i];
        EditorTab* opened = nullptr;
        switch (t.kind) {
            case TabKind::WORLD: opened = &world_tab; break;
            case TabKind::EDITOR_SETTINGS: opened = tabs.open<EditorSettingsTab>(); break;
            case TabKind::MODEL: if (t.guid.valid()) opened = tabs.open<ModelTab>(t.guid); break;
            case TabKind::MATERIAL: if (t.guid.valid()) opened = tabs.open<MaterialTab>(t.guid); break;
        }
        if (opened && i == saved.active) active = opened;
    }
    tabs.focus(active);
}

void EditorApplication::_load_state()
{
    std::ifstream in(Paths::roaming_data() / FILE_EDITOR_STATE, std::ios::binary);
    if (!in) return;

    toml::table parsed;
    try {
        parsed = toml::parse(in);
    } catch (const toml::parse_error&) {
        return;
    }
    const toml::table& tbl = parsed;

    settings.theme.preset = Theme::theme_preset_index(from_toml(tbl.at_path("theme.preset"), "Custom"));
    settings.theme.base = from_toml(tbl.at_path("theme.base"), settings.theme.base);
    settings.theme.accent = from_toml(tbl.at_path("theme.accent"), settings.theme.accent);
    settings.theme.text = from_toml(tbl.at_path("theme.text"), settings.theme.text);
    settings.theme.use_system_accent = tbl.at_path("theme.use_system_accent").value_or(settings.theme.use_system_accent);

    settings.vsync_mode = vsync_mode_from_name(tbl.at_path("display.vsync").value_or(std::string{}), settings.vsync_mode);
    settings.fps_cap = (int)tbl.at_path("display.fps_cap").value_or((int64_t)settings.fps_cap);

    if (auto v = tbl.at_path("window.custom_titlebar").value<bool>()) win32.window_set_custom_titlebar(*v);

    // older builds kept the layout and every project's tabs here; hold on to them so each project
    // picks its state up the first time it's opened, the next save drops them from this file
    _project_state_legacy.clear();
    if (tbl.contains("layout")) {
        if (const toml::table* t = tbl["layout"].as_table()) _project_state_legacy.insert_or_assign("layout", *t);
        if (const toml::table* t = tbl["panels"].as_table()) _project_state_legacy.insert_or_assign("panels", *t);
        if (auto sp = tbl.at_path("display.screen_percentage").value<double>()) {
            toml::table viewport;
            viewport.insert_or_assign("screen_percentage", *sp);
            _project_state_legacy.insert_or_assign("viewport", std::move(viewport));
        }
        if (const toml::table* t = tbl["project_tabs"].as_table()) _project_state_legacy.insert_or_assign("project_tabs", *t);
    }

    settings.theme.apply();
}

void EditorApplication::_save_state()
{
    const char* preset_name = Theme::theme_preset_name(settings.theme.preset);

    toml::table theme;
    theme.insert_or_assign("preset", preset_name ? preset_name : "");
    theme.insert_or_assign("base", to_toml(settings.theme.base));
    theme.insert_or_assign("accent", to_toml(settings.theme.accent));
    theme.insert_or_assign("text", to_toml(settings.theme.text));
    theme.insert_or_assign("use_system_accent", settings.theme.use_system_accent);

    toml::table window;
    window.insert_or_assign("custom_titlebar", static_cast<bool>(win32.window.custom_titlebar));

    toml::table display;
    display.insert_or_assign("vsync", vsync_mode_name(settings.vsync_mode));
    display.insert_or_assign("fps_cap", (int64_t)settings.fps_cap);

    toml::table root;
    root.insert_or_assign("theme", std::move(theme));
    root.insert_or_assign("window", std::move(window));
    root.insert_or_assign("display", std::move(display));

    std::ofstream out(Paths::roaming_data() / FILE_EDITOR_STATE, std::ios::binary);
    if (!out) return;
    out << root << '\n';
}

void EditorApplication::_load_project_state()
{
    toml::table parsed;
    bool found = false;
    std::ifstream in(project.editor_dir / FILE_PROJECT_STATE, std::ios::binary);
    if (in) {
        try {
            parsed = toml::parse(in);
            found = true;
        } catch (const toml::parse_error& e) {
            log_write("Editor: ignoring unreadable '%s' (%s)", (project.editor_dir / FILE_PROJECT_STATE).string().c_str(), std::string(e.description()).c_str());
        }
    }

    if (!found && !_project_state_legacy.empty()) {
        parsed = _project_state_legacy;
        parsed.erase("project_tabs");
        if (const toml::table* pt = _project_state_legacy.at_path("project_tabs").as_table()) {
            if (const toml::table* mine = (*pt)[project.root.generic_string()].as_table()) {
                toml::table tabs_tbl;
                tabs_tbl.insert_or_assign("active", (*mine)["active"].value_or((int64_t)0));
                if (const toml::array* list = (*mine)["tabs"].as_array()) tabs_tbl.insert_or_assign("open", *list);
                parsed.insert_or_assign("tabs", std::move(tabs_tbl));
            }
        }
        found = true;
    }

    // always start from the defaults so a project never inherits the previous one's layout,
    // then let whatever the file has override them
    _project_state_read(_project_state_default);
    if (found) _project_state_read(parsed);
}

void EditorApplication::_save_project_state()
{
    if (!project.loaded()) return;
    std::error_code ec;
    std::filesystem::create_directories(project.editor_dir, ec);
    if (ec) {
        log_write("Editor: failed to create '%s' (%s)", project.editor_dir.string().c_str(), ec.message().c_str());
        return;
    }
    std::ostringstream ss;
    ss << _project_state_write() << '\n';
    const std::string text = ss.str();
    if (!write_file_atomic(project.editor_dir / FILE_PROJECT_STATE, text.data(), text.size())) {
        log_write("Editor: failed to write '%s'", (project.editor_dir / FILE_PROJECT_STATE).string().c_str());
    }
}

toml::table EditorApplication::_project_state_write()
{
    EditorLayout& L = world_tab.layout;

    toml::table layout;
    layout.insert_or_assign("version", (int64_t)WorldTab::VERSION);
    layout.insert_or_assign("split_left", (double)L.split_left);
    layout.insert_or_assign("split_right", (double)L.split_right);
    layout.insert_or_assign("left_split_y", (double)L.left.split_y);
    layout.insert_or_assign("right_split_y", (double)L.right.split_y);
    layout.insert_or_assign("center_split", (double)L.center_split);
    layout.insert_or_assign("left_collapsed", (bool)L.left.collapsed);
    layout.insert_or_assign("right_collapsed", (bool)L.right.collapsed);
    for (DockWell* w : L.wells()) {
        layout.insert_or_assign(std::string("active_") + to_string(w->zone), w->active_name);
    }

    toml::table panels;
    for (const DockSlot& s : L.slots) {
        toml::table pt;
        pt.insert_or_assign("open", (bool)s.open);
        pt.insert_or_assign("zone", to_string(s.zone));
        panels.insert_or_assign(s.panel->layout_id, std::move(pt));
    }

    toml::table viewport;
    viewport.insert_or_assign("screen_percentage", (double)L.viewport.screen_percentage);

    toml::array open;
    int64_t active = 0;
    for (const EditorTab* t : tabs.tabs) {
        if (t == tabs.active) active = (int64_t)open.size();
        toml::table tt;
        tt.insert_or_assign("kind", std::string(_tab_kind_name(t->kind)));
        tt.insert_or_assign("guid", t->guid.to_string());
        open.push_back(std::move(tt));
    }
    toml::table tabs_tbl;
    tabs_tbl.insert_or_assign("active", active);
    tabs_tbl.insert_or_assign("open", std::move(open));

    toml::table root;
    root.insert_or_assign("layout", std::move(layout));
    root.insert_or_assign("panels", std::move(panels));
    root.insert_or_assign("viewport", std::move(viewport));
    root.insert_or_assign("tabs", std::move(tabs_tbl));
    return root;
}

void EditorApplication::_project_state_read(const toml::table& p_tbl)
{
    const toml::table& tbl = p_tbl;

    auto lv = tbl.at_path("layout");
    const int layout_version = (int)lv["version"].value_or((int64_t)-1);
    if (layout_version == 1 || layout_version == WorldTab::VERSION) {
        EditorLayout& L = world_tab.layout;
        const bool legacy = layout_version == 1;
        L.split_right = (float)lv[legacy ? "split_x" : "split_right"].value_or((double)L.split_right);
        L.right.split_y = (float)lv[legacy ? "split_y" : "right_split_y"].value_or((double)L.right.split_y);
        L.center_split = (float)lv["center_split"].value_or((double)L.center_split);
        L.right.collapsed = lv["right_collapsed"].value_or(L.right.collapsed);
        if (!legacy) {
            L.split_left = (float)lv["split_left"].value_or((double)L.split_left);
            L.left.split_y = (float)lv["left_split_y"].value_or((double)L.left.split_y);
            L.left.collapsed = lv["left_collapsed"].value_or(L.left.collapsed);
            for (DockWell* w : L.wells()) {
                std::string key = std::string("active_") + to_string(w->zone);
                if (auto name = lv[key].value<std::string>()) w->active_name = *name;
            }
        } else {
            L.right.top.active_name = lv["active_top"].value_or(std::string{});
            L.right.bottom.active_name = lv["active_bottom"].value_or(std::string{});
        }

        if (const toml::table* pans = tbl.at_path("panels").as_table()) {
            for (DockSlot& s : L.slots) {
                // older files keyed panels by their full title, icon included
                const char* key = pans->contains(s.panel->layout_id) ? s.panel->layout_id : s.panel->title;
                if (!pans->contains(key)) continue;
                auto pv = (*pans)[key];
                s.open = pv["open"].value_or(s.open);
                s.zone = dock_zone_from_string(pv["zone"].value_or(std::string(to_string(s.zone))), s.zone);
            }
        }
        if (legacy) {
            if (const toml::array* names = lv["drawer_open"].as_array()) {
                for (const toml::node& n : *names) {
                    if (DockSlot* s = L.find(n.value_or(std::string{}).c_str())) {
                        s->open = true;
                        s->zone = DockZone::BOTTOM;
                    }
                }
            }
            L.bottom.active_name = lv["drawer_active"].value_or(std::string{});
        }
        for (DockWell* w : L.wells()) {
            if (DockSlot* s = L.find(w->active_name.c_str())) w->active_name = s->panel->layout_id;
        }
    }

    float& sp = world_tab.layout.viewport.screen_percentage;
    sp = (float)tbl.at_path("viewport.screen_percentage").value_or((double)sp);
    sp = sp < 0.01f ? 0.01f : (sp > 1.0f ? 1.0f : sp);

    _saved_tabs = {};
    if (const toml::table* tt = tbl.at_path("tabs").as_table()) {
        _saved_tabs.active = (uint32_t)(*tt)["active"].value_or((int64_t)0);
        if (const toml::array* list = (*tt)["open"].as_array()) {
            for (const toml::node& n : *list) {
                const toml::table* t = n.as_table();
                if (!t) continue;
                TabKind kind;
                if (!_tab_kind_from_string((*t)["kind"].value_or(std::string{}), kind)) continue;
                _saved_tabs.tabs.push_back({ kind, Guid::from_string((*t)["guid"].value_or(std::string{})) });
            }
        }
    }
}

void EditorApplication::_titlebar_block(const TitlebarLayout& p_layout, ImVec2 p_min, ImVec2 p_max)
{
    win32.window_titlebar_add_rect(
        (long)(p_min.x - p_layout.origin.x), (long)(p_min.y - p_layout.origin.y),
        (long)(p_max.x - p_layout.origin.x), (long)(p_max.y - p_layout.origin.y)
    );
}

void EditorApplication::_draw_titlebar()
{
    const UiStyle& ui = ui_style();
    const float TAB_H = ui.titlebar.tab_height;
    const float BTN_W = ui.titlebar.caption_width;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ui.titlebar.menu_padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const bool show_tabs = project.loaded();
    const float MENU_H = ImGui::GetFrameHeight();
    const float H = MENU_H + TAB_H;

    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    if (!ImGui::BeginViewportSideBar("##LumenTitlebar", ImGui::GetMainViewport(), ImGuiDir_Up, H, flags)) {
        ImGui::PopStyleVar(2);
        ImGui::End();
        return;
    }

    TitlebarLayout L;
    L.origin = ImGui::GetWindowPos();
    L.width = ImGui::GetWindowWidth();
    L.menu_h = MENU_H;
    L.bar_h = H;
    L.tab_h = TAB_H;
    L.btn_w = BTN_W;
    L.logo = H;

    win32.window_titlebar_reset((int)H);

    const float width = ImGui::GetWindowWidth();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(L.origin.x, L.origin.y + L.menu_h), ImVec2(L.origin.x + width, L.origin.y + H), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    dl->AddRectFilled(ImVec2(L.origin.x, L.origin.y + H - ui.stroke.hairline), ImVec2(L.origin.x + width, L.origin.y + H), ui.color.line);

    _titlebar_menus(L);
    if (show_tabs) _titlebar_tabs(L);
    _titlebar_logo(L);

    if (frame_stats.frame_avg > 0.0f) {
        char perf[48];
        std::snprintf(perf, sizeof(perf), "%.2f ms  %.0f fps", frame_stats.frame_avg, 1000.0f / frame_stats.frame_avg);
        const ImVec2 ts = ImGui::CalcTextSize(perf);
        const ImVec2 pos(L.origin.x + L.width - ts.x - ui.titlebar.stats_margin, L.origin.y + L.menu_h + (L.tab_h - ts.y) * 0.5f);
        ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), perf);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}

void EditorApplication::_titlebar_menus(const TitlebarLayout& p_layout)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ui_menu_push_style();
    if (!ImGui::BeginMenuBar()) {
        ui_menu_pop_style();
        return;
    }

    if (project.loaded()) {
        ImGui::SetCursorPosX(p_layout.logo + ui_style().titlebar.menu_offset_x);
        float menu_x0 = ImGui::GetCursorScreenPos().x;
        _titlebar_editor_menu();
        float menu_x1 = ImGui::GetCursorScreenPos().x;
        if (menu_x1 > menu_x0) _titlebar_block(p_layout, ImVec2(menu_x0, p_layout.origin.y), ImVec2(menu_x1, p_layout.origin.y + p_layout.menu_h));
    }

    const std::string& title = project.name.empty() ? std::string("Lumen Editor") : project.name;
    ImVec2 ts = ImGui::CalcTextSize(title.c_str());
    float right_pad = win32.window.custom_titlebar ? (p_layout.width - p_layout.btn_w * 3.0f) : p_layout.width;
    float title_x = p_layout.origin.x + right_pad - ts.x - ui_style().titlebar.title_margin;
    dl->AddText(ImVec2(title_x, p_layout.origin.y + (p_layout.menu_h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());

    if (win32.window.custom_titlebar) _titlebar_caption_buttons(p_layout);

    ImGui::EndMenuBar();
    ui_menu_pop_style();
}

void EditorApplication::_titlebar_caption_buttons(const TitlebarLayout& p_layout)
{
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const UiStyle& ui = ui_style();

    const float btns_x = p_layout.origin.x + p_layout.width - p_layout.btn_w * 3.0f;
    fg->AddRectFilled(ImVec2(btns_x, p_layout.origin.y), ImVec2(btns_x + p_layout.btn_w * 3.0f, p_layout.origin.y + p_layout.menu_h), ImGui::GetColorU32(ImGuiCol_MenuBarBg));

    RECT rmin, rmax, rclose;
    auto client_rect = [&](float x0) -> RECT {
        return RECT{ (long)(x0 - p_layout.origin.x), (long)(p_layout.origin.y - p_layout.origin.y), (long)(x0 + p_layout.btn_w - p_layout.origin.x), (long)(p_layout.origin.y + p_layout.menu_h - p_layout.origin.y) };
    };

    ImVec2 mouse = ImGui::GetIO().MousePos;
    auto ctrl = [&](float x0, int glyph, bool danger) {
        ImVec2 p(x0, p_layout.origin.y);
        bool hovered = mouse.x >= x0 && mouse.x < x0 + p_layout.btn_w && mouse.y >= p_layout.origin.y && mouse.y < p_layout.origin.y + p_layout.menu_h;
        if (hovered) fg->AddRectFilled(p, ImVec2(p.x + p_layout.btn_w, p.y + p_layout.menu_h), danger ? ui.color.danger_strong : ui.color.hover);
        ImVec2 c(p.x + p_layout.btn_w * 0.5f, p.y + p_layout.menu_h * 0.5f);
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        const float s = ui.titlebar.caption_glyph;
        const float o = ui.titlebar.caption_glyph_offset;
        const float w = ui.titlebar.caption_stroke;
        switch (glyph) {
            case 0: fg->AddLine(ImVec2(c.x-s,c.y), ImVec2(c.x+s,c.y), col, w); break;
            case 1: fg->AddRect(ImVec2(c.x-s,c.y-s), ImVec2(c.x+s,c.y+s), col, 0,0,w); break;
            case 2:
                fg->AddRect(ImVec2(c.x-s+o,c.y-s-o), ImVec2(c.x+s+o,c.y+s-o), col, 0,0,w);
                fg->AddRectFilled(ImVec2(c.x-s-o,c.y-s+o), ImVec2(c.x+s-o,c.y+s+o), hovered ? ui.color.hover : ui.color.chrome);
                fg->AddRect(ImVec2(c.x-s-o,c.y-s+o), ImVec2(c.x+s-o,c.y+s+o), col, 0,0,w);
                break;
            case 3:
                fg->AddLine(ImVec2(c.x-s,c.y-s), ImVec2(c.x+s,c.y+s), col, ui.titlebar.caption_close_stroke);
                fg->AddLine(ImVec2(c.x-s,c.y+s), ImVec2(c.x+s,c.y-s), col, ui.titlebar.caption_close_stroke);
                break;
        }
    };

    float x_min = btns_x;
    float x_max = btns_x + p_layout.btn_w;
    float x_close = btns_x + p_layout.btn_w * 2.0f;

    ctrl(x_min, 0, false);
    ctrl(x_max, win32.window_is_maximized() ? 2 : 1, false);
    ctrl(x_close, 3, true);

    rmin = client_rect(x_min);
    rmax = client_rect(x_max);
    rclose = client_rect(x_close);
    win32.window_titlebar_set_controls(rmin, rmax, rclose);
}

void EditorApplication::_titlebar_tabs(const TitlebarLayout& p_layout)
{
    const UiStyle& ui = ui_style();
    const ImVec2 min(p_layout.origin.x + p_layout.logo + ui.titlebar.menu_offset_x, p_layout.origin.y + p_layout.menu_h);
    const ImVec2 max(p_layout.origin.x + p_layout.width, p_layout.origin.y + p_layout.bar_h);
    const float stats_w = ImGui::CalcTextSize("000.00 ms  0000 fps").x + ui.titlebar.stats_margin * 2.0f;
    tabs.draw_bar(min, max, stats_w, ImGui::GetColorU32(ImGuiCol_MenuBarBg), [&](ImVec2 p_min, ImVec2 p_max) { _titlebar_block(p_layout, p_min, p_max); });
}

void EditorApplication::_titlebar_logo(const TitlebarLayout& p_layout)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const UiStyle& ui = ui_style();
    const float m = ui.titlebar.logo_margin;
    const ImVec2 mn(p_layout.origin.x + m, p_layout.origin.y + m);
    const ImVec2 mx(p_layout.origin.x + p_layout.logo - m, p_layout.origin.y + p_layout.bar_h - m);

    VkDescriptorSet logo_set = imgui.texture_cache.get(resources.icon_image.image_view);
    dl->PushClipRect(p_layout.origin, ImVec2(p_layout.origin.x + p_layout.width, p_layout.origin.y + p_layout.bar_h), false);
    if (logo_set) dl->AddImage(logo_set, mn, mx);
    else dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_Text), ui.titlebar.logo_rounding);
    dl->PopClipRect();
}

void EditorApplication::_titlebar_editor_menu()
{
    if (ImGui::BeginMenu("File")) {
        // if (ImGui::MenuItem("Save Current Scene")) {}
        // if (ImGui::MenuItem("Save Current Scene As")) {}
        // if (ImGui::MenuItem("Save", "Ctrl+S", false, tabs.active && tabs.active->dirty())) {
            // tabs.active->save(_context());
        // }
        // if (ImGui::MenuItem("Save All")) project.save();
        // if (ImGui::MenuItem("Choose Files to Save")) {}

        // ImGui::Separator();
        // if (ImGui::MenuItem("New")) {}
        // if (ImGui::MenuItem("Open")) {}
        // if (ImGui::MenuItem("Zip Project")) {}
        // if (ImGui::MenuItem("Open Current Project Directory")) Paths::reveal_in_explorer(project.root);
        // if (ImGui::MenuItem("Recent Projects")) {}

        // ImGui::Separator();

        if (ImGui::MenuItem("Export Project...")) ProjectExporter::run_with_dialog(project);
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) close_project();
        if (ImGui::MenuItem("Quit", "Alt+F4")) win32.window_request_close();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        // if (ImGui::MenuItem("Undo")) {}
        // if (ImGui::MenuItem("Redo")) {}
        // if (ImGui::MenuItem("Undo History")) {}
        // ImGui::Separator();
        // ImGui::BeginDisabled(true);
        // if (ImGui::MenuItem("Cut")) {}
        // if (ImGui::MenuItem("Copy")) {}
        // if (ImGui::MenuItem("Paste")) {}
        // if (ImGui::MenuItem("Duplicate")) {}
        // if (ImGui::MenuItem("Delete")) {}
        // ImGui::EndDisabled();
        // ImGui::Separator();
        if (ImGui::MenuItem("Editor Settings")) tabs.open<EditorSettingsTab>();
        if (ImGui::MenuItem("Project Settings")) {
            world_tab.layout.open<ProjectSettingsPanel>();
            tabs.focus(&world_tab);
        }
        if (ImGui::MenuItem("Open Editor Data Folder")) Paths::reveal_in_explorer(Paths::roaming_data());
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        world_tab.draw_menu();
        if (ImGui::BeginMenu("Display")) {
            int vsync = (int)settings.vsync_mode;
            if (ImGui::Combo("VSync", &vsync, VSYNC_MODE_NAMES, (int)std::size(VSYNC_MODE_NAMES))) {
                settings.vsync_mode = (VsyncMode)vsync;
                settings.dirty = true;
            }
            if (ImGui::DragInt("FPS Cap", &settings.fps_cap, 1.0f, 0, 1000, settings.fps_cap == 0 ? "Unlimited" : "%d", ImGuiSliderFlags_AlwaysClamp)) settings.dirty = true;
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // if (ImGui::MenuItem("Enable Fullscreen", "Alt+F11")) {}
        if (ImGui::MenuItem("Take Screenshot", "Ctrl+F12")) screenshot_request();
        ImGui::EndMenu();
    }

    // if (ImGui::BeginMenu("Tools")) {
    //     ImGui::EndMenu();
    // }
}

EditorContext& EditorApplication::_context()
{
    _ctx.render_path = dynamic_cast<EditorRenderPath*>(render_path);
    return _ctx;
}

void EditorApplication::update_camera(float p_dt)
{
    (void)p_dt;
    editor_camera.update(0.0f);
}

const Camera& EditorApplication::active_camera() const
{
    if (tabs.active) {
        if (const Camera* c = tabs.active->camera()) return *c;
    }
    return editor_camera.camera;
}

const World& EditorApplication::render_world() const
{
    if (tabs.active) {
        if (const World* w = tabs.active->scene()) return *w;
    }
    return world;
}

}
