#include <editor/assets/builtin_shapes.h>
#include <editor/application/editor_application.h>
#include <core/io/embedded_resource.h>
#include <core/io/file_io.h>
#include <core/base/hash.h>
#include <core/io/path.h>
#include <core/version.h>
#include <editor/assets/content_cooker.h>
#include <editor/assets/import/icon_cooker.h>
#include <editor/assets/import/splash_cooker.h>
#include <editor/export/project_exporter.h>
#include <editor/documents/material/material_document.h>
#include <editor/documents/model/model_document.h>
#include <editor/documents/texture/texture_document.h>
#include <editor/tools/settings/editor_settings_tool.h>
#include <editor/tools/settings/project_settings_tool.h>
#include <editor/documents/mesh/mesh_document.h>
#include <editor/documents/animation/animation_document.h>
#include <editor/documents/terrain/terrain_document.h>
#include <editor/tools/version_control/version_control_tool.h>
#include <editor/documents/asset_document.h>
#include <core/scene/template.h>
#include <algorithm>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <drivers/windows/dialogs_win32.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <windows.h>

namespace lumen {

static constexpr float SCENE_CAMERA_MIN_DISTANCE = 0.05f;
static constexpr float SCENE_CAMERA_MAX_DISTANCE = 20000.0f;
static constexpr float SCENE_CAMERA_DISTANCE = 5.0f;

Error EditorApplication::on_init()
{
    using enum Error;
    Error err;

    ui_style().build();
    ui_style().apply_to_imgui();

    win32.window_set_custom_titlebar(main_window, true);
    win32.window_set_title(main_window, "Lumen Editor");

    err = resources.initialize(dd);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    err = win32.window_set_icon(main_window, EmbeddedResource::load_icon(L"LUMEN_ICON"));
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    ImGuiIO& io = ImGui::GetIO();
    {
        EmbeddedResource::Blob inter = EmbeddedResource::load(L"FONTS_INTER_REGULAR_TTF");
        ImFontConfig inter_cfg;
        inter_cfg.FontDataOwnedByAtlas = false;
        inter_cfg.OversampleH = 2;
        io.Fonts->AddFontFromMemoryTTF((void*)inter.data, (int)inter.size, ui_style().font.size, &inter_cfg);

        EmbeddedResource::Blob fa = EmbeddedResource::load(L"FONTS_FA_SOLID_900_OTF");
        static const ImWchar fa_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        ImFontConfig fa_cfg;
        fa_cfg.MergeMode = true;
        fa_cfg.PixelSnapH = true;
        fa_cfg.GlyphOffset.y = ui_style().font.icon_offset_y;
        fa_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)fa.data, (int)fa.size, ui_style().font.icon_size, &fa_cfg, fa_ranges);
        io.Fonts->Build();
    }

    err = project_manager.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    git.log_callback = [](const std::string& p_line) { log_write("[Git] %s", p_line.c_str()); };
    workspace.initialize();
    err = play.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = run.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = texture_viewer.initialize(dd);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = thumbnails.initialize(dd);
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    _ctx.win32 = &win32;
    _ctx.main_window = &main_window;
    _ctx.imgui = &imgui;
    _ctx.git = &git;
    _ctx.renderer = &renderer;
    _ctx.project = &project;
    _ctx.scene = &scene;
    _ctx.main_scene = &scene;
    template_library().resolve = [this](Guid p_guid) {
        const AssetRegistry::Entry* e = assets.find(p_guid);
        return e && e->type == AssetType::TEMPLATE ? e->path : std::filesystem::path{};
    };
    _ctx.project_manager = &project_manager;
    _ctx.scene_document = &workspace.scene;
    _ctx.documents = &documents;
    _ctx.selection = &selection;
    _ctx.main_selection = &selection;
    editor_camera.min_distance = SCENE_CAMERA_MIN_DISTANCE;
    editor_camera.max_distance = SCENE_CAMERA_MAX_DISTANCE;
    editor_camera.distance = SCENE_CAMERA_DISTANCE;
    _ctx.editor_camera = &editor_camera;
    _ctx.frame_stats = &frame_stats;
    _ctx.settings = &settings;
    _ctx.profiling = &profiling;
    _ctx.resources = &resources;
    _ctx.imports = &imports;
    _ctx.assets = &assets;
    _ctx.texture_viewer = &texture_viewer;
    _ctx.import_dialog = &import_dialog;
    _ctx.references_dialog = &references_dialog;
    _ctx.reload_asset_documents = [this](const std::vector<std::filesystem::path>& p_paths) {
        std::vector<EditorDocument*> reload;
        for (auto& w : documents.documents) {
            const std::filesystem::path* path = w->asset_path();
            if (path && std::find(p_paths.begin(), p_paths.end(), *path) != p_paths.end()) reload.push_back(w.get());
        }
        for (EditorDocument* w : reload) _reload_document(w);
    };
    _ctx.thumbnails = &thumbnails;
    _ctx.open_project_callback = [this](const std::filesystem::path& p_root) { open_project(p_root); };
    _ctx.close_project_callback = [this] { close_project(); };
    _ctx.save_all_callback = [this] { save_all(); };
    _ctx.reload_project_callback = [this](std::function<void()> p_op) { reload_project(std::move(p_op)); };
    _ctx.play = &play;
    play.physics_system.renderer = &renderer;
    play.physics_system.project = &project;
    _ctx.pie_is_playing = [this] { return play.active(); };
    _ctx.pie_toggle_play = [this] {
        if (play.active()) play.stop();
        else _play_start();
    };
    _ctx.pie_play = [this] {
        if (!play.active()) {
            _play_start();
            return;
        }
        if (!play.simulating) return;
        play.simulating = false;
        _keep_editor_view();
    };
    _ctx.pie_simulate = [this] {
        if (!play.active()) {
            _play_start();
            play.simulating = play.active();
            return;
        }
        if (play.simulating) return;
        editor_camera.place(play.session.camera);
        play.simulating = true;
    };
    _ctx.pie_is_simulating = [this] { return play.active() && play.simulating; };
    _ctx.pie_is_paused = [this] { return play.editing(); };
    _ctx.pie_toggle_pause = [this] {
        if (play.game()) editor_camera.place(play.session.camera);
        play.toggle();
        _keep_editor_view();
    };
    _ctx.pie_step = [this] { play.step(); };
    _ctx.pie_is_scaled = [this] { return play.scaled; };
    _ctx.pie_toggle_scaled = [this] {
        const bool back_to_pause = play.scaled && play.scaled_from_pause;
        play.set_scaled(!play.scaled);
        if (back_to_pause && play.editing()) editor_camera.place(play.session.camera);
        _keep_editor_view();
    };
    _ctx.run_is_running = [this](const std::filesystem::path& p_root) { return run.is_running_for(p_root); };
    _ctx.run_toggle = [this](const std::filesystem::path& p_root) {
        if (run.is_running_for(p_root)) { run.stop(); return; }
        ContentCooker::cook_root(p_root);
        run.relaunch(p_root);
    };

    git.ignore_lines = { std::string("/") + Project::DIR_EDITOR + "/" };
    git.lfs_patterns = { std::string(Project::DIR_CONTENT) + "/**/*.bin" };
    _project_state_default = _project_state_write();
    _load_state();
    _apply_theme();
    return OK;
}

void EditorApplication::_apply_theme()
{
    settings.theme.apply();
    if (win32.window_set_titlebar_color(main_window, (COLORREF)Theme::to_colorref(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg])) != Error::OK) log_write("Editor: custom titlebar color unsupported (needs Windows 11).");
}

void EditorApplication::on_shutdown()
{
    EditorContext& ctx = _context();
    _save_project_state();
    documents.clear(ctx);
    workspace.shutdown(ctx);
    texture_viewer.shutdown();
    thumbnails.shutdown(ctx);

    play.shutdown();
    run.shutdown();
    git.shutdown();
    resources.shutdown();

    _save_state();
    project_manager.save_recents();
    project_manager.shutdown();
}

void EditorApplication::_play_start()
{
    if (!workspace.scene.has_world() || workspace.scene.active_template()) return;
    play.start(scene, workspace.scene.guid, editor_camera.camera);
}

void EditorApplication::on_simulate(float p_dt)
{
    if (!play.active()) return;
    if (!project.loaded() || !workspace.scene.has_world() || workspace.scene.guid != play.scene_guid || workspace.scene.active_template()) {
        play.stop();
        return;
    }
    Viewport& viewport = workspace.scene.viewport;
    play.session.input.read(viewport.game_captured, vec2(viewport.game_look.x, viewport.game_look.y), p_dt);
    if (play.simulating) play.session.camera = editor_camera.camera;
    viewport.game_look = ImVec2(0.0f, 0.0f);
    play.update(p_dt);
}

void EditorApplication::_save_focused()
{
    EditorContext& ctx = _context();
    workspace.save(ctx);
}

void EditorApplication::on_update(float)
{
    if (settings.theme_pending) {
        settings.theme_pending = false;
        _apply_theme();
    }
    run.poll();
    _draw_titlebar();

    EditorContext& ctx = _context();
    if (project.loaded()) {
        if (loading_marker && !pending_render_path) _clear_loading_marker();
        _tick_imports();
        assets.tick();
        selection.prune(scene);

        documents.update(ctx);
        workspace.tick(ctx);
        workspace.scene.bind(ctx);
        workspace.draw(ctx);
        texture_viewer.collect(ctx.renderer->frame_number, ctx.renderer->frame_count);
        import_dialog.draw(ctx);
        references_dialog.draw(ctx);
        thumbnails.update(ctx);
        if (!workspace.scene.viewport.game_captured) {
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) _save_focused();
            if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) save_all();
        }

        const uint64_t signature = _documents_hash();
        if (signature != _documents_signature) {
            _documents_signature = signature;
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
    win32.notify(main_window, "Import complete", message);
    win32.window_flash(main_window);
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
        if (c.keep_loaded) continue;
        LAssetHeader ah{};
        if (!read_asset_header(c.content_bin, ah)) continue;
        switch (ah.type) {
            case AssetType::TEXTURE:
                if (const LTexture* old = renderer.textures.get(c.guid)) imgui.texture_cache.retire(old->image.image_view, renderer.frame_number, renderer.frame_count);
                renderer.textures.unload(c.guid);
                renderer.textures.load(c.guid, c.content_bin);
                if (c.guid == project.settings.window_icon) IconCooker::cook_icon(project);
                if (c.guid == project.settings.splash_image) SplashCooker::cook_splash(project);
                break;
            case AssetType::MESH:
                renderer.geometry.unload(c.guid);
                renderer.geometry.load(c.guid, c.content_bin);
                renderer.skeletons.load_mesh(c.guid, c.content_bin);
                break;
            case AssetType::TERRAIN:
                renderer.terrain.reload(c.guid, c.content_bin);
                break;
            default:
                break;
        }
    }
    std::vector<EditorDocument*> reload;
    for (auto& w : documents.documents) {
        const std::filesystem::path* path = w->asset_path();
        if (!path) continue;
        const bool hit = std::any_of(imports.completed.begin(), imports.completed.end(), [&](const AssetImportTracker::Completed& c) { return c.dst == *path || c.guid == w->guid; });
        if (hit) reload.push_back(w.get());
    }
    imports.completed.clear();
    imports_notified = 0;
    for (EditorDocument* w : reload) _reload_document(w);
}

EditorDocument* EditorApplication::_open_document(DocumentKind p_kind, Guid p_guid)
{
    switch (p_kind) {
        case DocumentKind::MODEL: return documents.open<ModelDocument>(p_guid);
        case DocumentKind::MATERIAL: return documents.open<MaterialDocument>(p_guid);
        case DocumentKind::TEXTURE: return documents.open<TextureDocument>(p_guid);
        case DocumentKind::MESH: return documents.open<MeshDocument>(p_guid);
        case DocumentKind::ANIMATION: return documents.open<AnimationDocument>(p_guid);
        case DocumentKind::TERRAIN: return documents.open<TerrainDocument>(p_guid);
        default: return nullptr;
    }
}

void EditorApplication::_reload_document(EditorDocument* p_document)
{
    EditorContext& ctx = _context();
    const size_t index = documents._index_of(p_document->kind, p_document->guid);
    if (index == DocumentManager::NPOS || !p_document->asset_path()) return;

    const DocumentKind kind = p_document->kind;
    const Guid guid = p_document->guid;
    const AssetDocument* old = static_cast<const AssetDocument*>(p_document);
    const OrbitCamera orbit = old->orbit;
    const std::filesystem::path path = old->path;
    const bool was_active = workspace.active_document == p_document;

    documents._remove_at(ctx, index);
    if (was_active) workspace.active_document = nullptr;
    EditorDocument* fresh = _open_document(kind, guid);
    if (!fresh) return;
    fresh->_focus_request = was_active;
    if (was_active) workspace.active_document = fresh;
    std::unique_ptr<EditorDocument> moved = std::move(documents.documents.back());
    documents.documents.pop_back();
    documents.documents.insert(documents.documents.begin() + (std::ptrdiff_t)std::min(index, documents.documents.size()), std::move(moved));
    AssetDocument* asset = static_cast<AssetDocument*>(fresh);
    asset->path = path;
    asset->restore_view(orbit);
}

Error EditorApplication::open_project(const std::filesystem::path& p_root)
{
    using enum Error;

    _write_loading_marker(p_root);
    Error err = project_load(p_root);
    if (err != OK) {
        project_unload();
        _clear_loading_marker();
    }
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    builtin_shapes_prepare(project, renderer);
    session_project = project.root;

    AssetImportTracker::clear_staging(project);
    assets.open(project.assets_dir);
    selection.clear();
    render_path_request(new EditorRenderPath());
    project_manager.add_recent(project.root, project.name);
    if (!run.is_running_for(p_root)) {
        run.pending_root.clear();
        run.stop();
    }
    EditorContext& ctx = _context();
    documents.clear(ctx);
    workspace.reset(ctx);
    _load_project_state();
    _restore_documents();
    _documents_signature = _documents_hash();
    git.initialize(project.root);


    if (project.settings.window_icon.valid() && IconCooker::stale(project.content_path(project.settings.window_icon), project.icon_path())) {
        IconCooker::cook_icon(project);
    }
    if (project.settings.splash_image.valid() && IconCooker::stale(project.content_path(project.settings.splash_image), project.splash_path())) {
        SplashCooker::cook_splash(project);
    }

    return OK;
}

bool EditorApplication::_confirm_cancel_imports(const wchar_t* p_action)
{
    const uint32_t count = imports.active_count();
    if (count > 0) {
        const std::wstring heading = count == 1 ? L"1 item is still importing" : std::to_wstring(count) + L" items are still importing";
        const std::wstring body = std::wstring(p_action) + L" will cancel every import in progress.";
        if (!drivers::Win32Dialogs::ask(L"Lumen Editor", heading.c_str(), body.c_str(), L"Cancel Imports", L"Keep Importing")) return false;
        imports.cancel_all();
    }
    imports.wait_all();
    imports.completed.clear();
    imports_notified = 0;
    return true;
}

bool EditorApplication::on_close_requested()
{
    EditorContext& ctx = _context();
    return workspace.scene.confirm_discard_all(ctx) && documents.confirm_discard_all(ctx) && _confirm_cancel_imports(L"Closing the editor");
}

bool EditorApplication::close_project()
{
    EditorContext& ctx = _context();
    if (!workspace.scene.confirm_discard_all(ctx) || !documents.confirm_discard_all(ctx) || !_confirm_cancel_imports(L"Closing the project")) return false;
    session_project.clear();
    play.stop();
    pending_transition = [this]{
        EditorContext& ctx = _context();
        _save_project_state();
        _save_state();
        import_dialog.clear();
        documents.clear(ctx);
        workspace.reset(ctx);
        thumbnails.close(ctx);
        assets.close();
        selection.clear();
        template_library().clear();
        project_unload();
    };
    render_path_request(new ProjectManagerRenderPath());
    return true;
}

void EditorApplication::save_all()
{
    EditorContext& ctx = _context();
    workspace.scene.save_all(ctx);
    for (auto& w : documents.documents) {
        if (w->dirty() && !w->locked(ctx)) w->save(ctx);
    }
}

void EditorApplication::reload_project(std::function<void()> p_while_closed)
{
    if (!project.loaded() || !reopen_root.empty()) return;
    if (!close_project()) return;
    reopen_root = project.root;
    std::function<void()> close = std::move(pending_transition);
    pending_transition = [close = std::move(close), op = std::move(p_while_closed)] {
        close();
        if (op) op();
    };
}

static const char* _document_kind_name(DocumentKind p_kind)
{
    switch (p_kind) {
        case DocumentKind::EDITOR_SETTINGS: return "editor_settings";
        case DocumentKind::PROJECT_SETTINGS: return "project_settings";
        case DocumentKind::MODEL: return "model";
        case DocumentKind::MATERIAL: return "material";
        case DocumentKind::TEXTURE: return "texture";
        case DocumentKind::MESH: return "mesh";
        case DocumentKind::ANIMATION: return "animation";
        case DocumentKind::TERRAIN: return "terrain";
        case DocumentKind::VERSION_CONTROL: return "version_control";
        case DocumentKind::GPU_PROFILER: return "gpu_profiler";
        case DocumentKind::MEMORY: return "memory";
    }
    return "model";
}

static bool _document_kind_from_string(const std::string& p_name, DocumentKind& r_kind)
{
    static constexpr DocumentKind ALL[] = { DocumentKind::EDITOR_SETTINGS, DocumentKind::PROJECT_SETTINGS, DocumentKind::MODEL, DocumentKind::MATERIAL, DocumentKind::TEXTURE, DocumentKind::MESH, DocumentKind::ANIMATION, DocumentKind::TERRAIN, DocumentKind::VERSION_CONTROL, DocumentKind::GPU_PROFILER, DocumentKind::MEMORY };
    for (DocumentKind k : ALL) {
        if (p_name != _document_kind_name(k)) continue;
        r_kind = k;
        return true;
    }
    return false;
}

uint64_t EditorApplication::_documents_hash() const
{
    uint64_t h = HASH_FNV_OFFSET;
    for (const auto& w : documents.documents) {
        h = hash_fnv_step(h, (uint64_t)w->kind);
        h = hash_fnv_step(h, w->guid.value);
    }
    h = hash_fnv_step(h, workspace.scene.guid.value);
    h = hash_fnv_step(h, (uint64_t)(workspace.layout.left_w * 4.0f));
    h = hash_fnv_step(h, (uint64_t)(workspace.layout.right_w * 4.0f));
    h = hash_fnv_step(h, (uint64_t)(workspace.layout.left_split_y * 1000.0f));
    h = hash_fnv_step(h, (uint64_t)(workspace.layout.right_split_y * 1000.0f));
    h = hash_fnv_step(h, (uint64_t)(workspace.layout.bottom_h * 4.0f));
    for (const EditorLayout::Slot& slot : workspace.layout.slots) h = hash_fnv_step(hash_fnv_step(h, (uint64_t)slot.zone), slot.open ? 1u : 0u);
    for (const auto& [name, zone] : workspace.layout.document_zones) h = hash_fnv_step(h, (uint64_t)zone);
    return h;
}

void EditorApplication::_restore_documents()
{
    if (_saved_scene.valid()) workspace.scene.open(_saved_scene);
    for (const SavedDocument& s : _saved_documents) {
        EditorDocument* opened = s.guid.valid() ? _open_document(s.kind, s.guid) : nullptr;
        if (opened) opened->_focus_request = false;
    }
}

void EditorApplication::_queue_last_project(const std::filesystem::path& p_last)
{
    if (p_last.empty() || !settings.reopen_last_project) return;
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        log_write("Editor: Shift held, not reopening '%s'.", p_last.string().c_str());
        return;
    }
    std::error_code ec;
    if (std::filesystem::exists(Paths::roaming_data() / FILE_LOADING_MARKER, ec)) {
        log_write("Editor: '%s' didn't finish opening last time, showing the project manager.", p_last.string().c_str());
        std::filesystem::remove(Paths::roaming_data() / FILE_LOADING_MARKER, ec);
        return;
    }
    if (!std::filesystem::exists(p_last / Project::FILE_NAME, ec)) {
        log_write("Editor: last project '%s' is gone, showing the project manager.", p_last.string().c_str());
        return;
    }
    reopen_root = p_last;
}

void EditorApplication::_write_loading_marker(const std::filesystem::path& p_root)
{
    const std::string root = p_root.generic_string();
    loading_marker = write_file_atomic(Paths::roaming_data() / FILE_LOADING_MARKER, root.data(), root.size());
}

void EditorApplication::_clear_loading_marker()
{
    std::error_code ec;
    std::filesystem::remove(Paths::roaming_data() / FILE_LOADING_MARKER, ec);
    loading_marker = false;
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

    settings.vsync_mode = vsync_mode_from_name(tbl.at_path("display.vsync").value_or(std::string{}), settings.vsync_mode);
    settings.fps_cap = (int)tbl.at_path("display.fps_cap").value_or((int64_t)settings.fps_cap);
    settings.theme.light = tbl.at_path("appearance.theme").value_or(std::string("dark")) == "light";


    settings.reopen_last_project = tbl.at_path("session.reopen_last_project").value_or(settings.reopen_last_project);
    _queue_last_project(std::filesystem::path(tbl.at_path("session.last_project").value_or(std::string{})));
}

void EditorApplication::_save_state()
{
    toml::table display;
    display.insert_or_assign("vsync", vsync_mode_name(settings.vsync_mode));
    display.insert_or_assign("fps_cap", (int64_t)settings.fps_cap);

    toml::table appearance;
    appearance.insert_or_assign("theme", settings.theme.light ? "light" : "dark");

    toml::table session;
    session.insert_or_assign("reopen_last_project", settings.reopen_last_project);
    session.insert_or_assign("last_project", session_project.generic_string());

    toml::table root;
    root.insert_or_assign("session", std::move(session));
    root.insert_or_assign("display", std::move(display));
    root.insert_or_assign("appearance", std::move(appearance));

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
    const EditorLayout& L = workspace.layout;
    toml::table layout;
    layout.insert_or_assign("version", (int64_t)Workspace::LAYOUT_VERSION);
    layout.insert_or_assign("left_width", (double)L.left_w);
    layout.insert_or_assign("right_width", (double)L.right_w);
    layout.insert_or_assign("left_split_y", (double)L.left_split_y);
    layout.insert_or_assign("right_split_y", (double)L.right_split_y);
    layout.insert_or_assign("bottom_height", (double)L.bottom_h);
    toml::table panels;
    toml::table panels_open;
    for (const EditorLayout::Slot& slot : L.slots) {
        panels.insert_or_assign(slot.panel->name(), std::string(to_string(slot.zone)));
        panels_open.insert_or_assign(slot.panel->name(), slot.open);
    }
    for (const auto& [name, zone] : L.document_zones) panels.insert_or_assign(name, std::string(to_string(zone)));
    layout.insert_or_assign("panels", std::move(panels));
    layout.insert_or_assign("panels_open", std::move(panels_open));
    toml::table active;
    for (size_t i = 0; i < EditorLayout::ZONES; i++) active.insert_or_assign(to_string((DockZone)i), L.active[i]);
    layout.insert_or_assign("active", std::move(active));

    toml::array open;
    for (const auto& w : documents.documents) {
        toml::table tt;
        tt.insert_or_assign("kind", std::string(_document_kind_name(w->kind)));
        tt.insert_or_assign("guid", w->guid.to_string());
        open.push_back(std::move(tt));
    }

    toml::table root;
    root.insert_or_assign("layout", std::move(layout));
    root.insert_or_assign("scene", workspace.scene.guid.to_string());
    root.insert_or_assign("documents", std::move(open));
    return root;
}

void EditorApplication::_project_state_read(const toml::table& p_tbl)
{
    auto lv = p_tbl.at_path("layout");
    if ((int)lv["version"].value_or((int64_t)-1) == Workspace::LAYOUT_VERSION) {
        EditorLayout& L = workspace.layout;
        L.left_w = (float)lv["left_width"].value_or((double)L.left_w);
        L.right_w = (float)lv["right_width"].value_or((double)L.right_w);
        L.left_split_y = (float)lv["left_split_y"].value_or((double)L.left_split_y);
        L.right_split_y = (float)lv["right_split_y"].value_or((double)L.right_split_y);
        L.bottom_h = (float)lv["bottom_height"].value_or((double)L.bottom_h);
        for (EditorLayout::Slot& slot : L.slots) {
            if (auto z = lv["panels"][slot.panel->name()].value<std::string>()) slot.zone = dock_zone_from_string(*z, slot.zone);
            if (auto o = lv["panels_open"][slot.panel->name()].value<bool>()) slot.open = *o;
        }
        if (const toml::table* panels = lv["panels"].as_table()) {
            for (const auto& [key, value] : *panels) {
                const std::string name(key.str());
                if (L.find(name)) continue;
                if (auto z = value.value<std::string>()) L.document_zones[name] = dock_zone_from_string(*z, DockZone::LEFT_TOP);
            }
        }
        for (size_t i = 0; i < EditorLayout::ZONES; i++) L.active[i] = lv["active"][to_string((DockZone)i)].value_or(L.active[i]);
    }

    _saved_scene = Guid::from_string(p_tbl["scene"].value_or(std::string{}));
    if (!_saved_scene.valid()) {
        if (const toml::array* list = p_tbl.at_path("tabs.open").as_array()) {
            for (const toml::node& n : *list) {
                const toml::table* t = n.as_table();
                if (t && (*t)["kind"].value_or(std::string{}) == "scene") _saved_scene = Guid::from_string((*t)["guid"].value_or(std::string{}));
            }
        }
    }

    _saved_documents.clear();
    if (const toml::array* list = p_tbl["documents"].as_array()) {
        for (const toml::node& n : *list) {
            const toml::table* t = n.as_table();
            if (!t) continue;
            DocumentKind kind;
            if (!_document_kind_from_string((*t)["kind"].value_or(std::string{}), kind)) continue;
            _saved_documents.push_back({ kind, Guid::from_string((*t)["guid"].value_or(std::string{})) });
        }
    }
}

/******************/
/**** TITLEBAR ****/
/******************/

void EditorApplication::_titlebar_block(const TitlebarLayout& p_layout, ImVec2 p_min, ImVec2 p_max)
{
    win32.window_titlebar_add_rect(main_window,
        (long)(p_min.x - p_layout.origin.x), (long)(p_min.y - p_layout.origin.y),
        (long)(p_max.x - p_layout.origin.x), (long)(p_max.y - p_layout.origin.y)
    );
}

struct TopbarMenus
{
    float x = 0.0f;
    float y = 0.0f;
    float h = 0.0f;
    ImGuiID open = 0;
};

static TopbarMenus g_topbar;

static bool _topbar_menu(const char* p_label)
{
    const UiStyle& ui = ui_style();
    const ImVec2 ts = ImGui::CalcTextSize(p_label);
    const ImVec2 a(g_topbar.x, g_topbar.y);
    const ImVec2 b(a.x + ts.x + ui.titlebar.menu_padding.x * 2.0f, a.y + g_topbar.h);
    g_topbar.x = b.x;

    ImGui::SetCursorScreenPos(a);
    const ImGuiID id = ImGui::GetID(p_label);
    const bool open_now = ImGui::IsPopupOpen(id, ImGuiPopupFlags_None);
    const bool pressed = ImGui::InvisibleButton(p_label, ImVec2(b.x - a.x, b.y - a.y), ImGuiButtonFlags_PressedOnClick);
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    if (pressed && !open_now) ImGui::OpenPopupEx(id);
    else if (hovered && !open_now && g_topbar.open != 0 && g_topbar.open != id && ImGui::IsPopupOpen(g_topbar.open, ImGuiPopupFlags_None)) ImGui::OpenPopupEx(id);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (open_now || hovered) dl->AddRectFilled(a, b, ui.color.topbar_hover);
    ui_text_centered(dl, a, b, ui.color.text, p_label);

    ImGui::SetNextWindowPos(ImVec2(a.x, b.y), ImGuiCond_Always);
    ui_menu_push_style();
    const bool open = ImGui::BeginPopupEx(id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!open) {
        ui_menu_pop_style();
        return false;
    }
    g_topbar.open = id;
    return true;
}

static void _topbar_menu_end()
{
    ImGui::EndPopup();
    ui_menu_pop_style();
}

void EditorApplication::_draw_titlebar()
{
    const UiStyle& ui = ui_style();
    const bool loaded = project.loaded();
    const float H = ui.titlebar.height;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui.color.topbar);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    const bool titlebar_open = ImGui::BeginViewportSideBar("##LumenTitlebar", ImGui::GetMainViewport(), ImGuiDir_Up, H, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    if (titlebar_open) {
        TitlebarLayout L;
        L.origin = ImGui::GetWindowPos();
        L.width = ImGui::GetWindowWidth();
        L.height = H;
        L.btn_w = ui.titlebar.caption_width;
        L.logo = H;
        win32.window_titlebar_reset(main_window, (int)H);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(L.origin, ImVec2(L.origin.x + L.width, L.origin.y + H), ui.color.topbar);

        float right = L.origin.x + L.width - L.btn_w * 3.0f;
        float menus_end = L.origin.x + L.logo;
        if (loaded) {
            g_topbar.x = L.origin.x + L.logo;
            g_topbar.y = L.origin.y;
            g_topbar.h = H - 1.0f;
            const float menus_x = g_topbar.x;
            _titlebar_menus();
            menus_end = g_topbar.x;
            _titlebar_block(L, ImVec2(menus_x, L.origin.y), ImVec2(menus_end, L.origin.y + H));
            right = _titlebar_search(L, right);
        }
        _titlebar_title(L, menus_end, right);
        _titlebar_logo(L);
        _titlebar_caption_buttons(L);
    }
    ImGui::End();
}

void EditorApplication::_titlebar_title(const TitlebarLayout& p_layout, float p_menus_end, float p_right)
{
    const UiStyle& ui = ui_style();
    std::string title = "Lumen Editor";
    if (project.loaded()) {
        const std::string doc = workspace.document_name();
        title = doc.empty() ? project.name : doc;
        if (workspace.dirty()) title += "*";
    }
    const ImVec2 ts = ImGui::CalcTextSize(title.c_str());
    float x = ImFloor(p_layout.origin.x + (p_layout.width - ts.x) * 0.5f);
    x = ImClamp(x, p_menus_end + ui.space.xl, ImMax(p_menus_end + ui.space.xl, p_right - ts.x - ui.space.xl));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2(p_menus_end, p_layout.origin.y), ImVec2(p_right, p_layout.origin.y + p_layout.height), true);
    dl->AddText(ImVec2(x, ImFloor(p_layout.origin.y + (p_layout.height - ts.y) * 0.5f)), ui.color.text_dim, title.c_str());
    dl->PopClipRect();
}

float EditorApplication::_titlebar_search(const TitlebarLayout& p_layout, float p_right)
{
    const UiStyle& ui = ui_style();
    const float w = ImFloor(ui.titlebar.search_width);
    const float h = ImFloor(ImGui::GetFontSize() + ui.space.md);
    const ImVec2 a(p_right - w - ui.space.lg, ImFloor(p_layout.origin.y + (p_layout.height - h) * 0.5f));
    const ImVec2 b(a.x + w, a.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, b, ui.color.search, ui.radius.sm);

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ui.color.transparent);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ui.color.transparent);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ui.color.transparent);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui.space.sm, ImFloor((h - ImGui::GetFontSize()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::SetCursorScreenPos(a);
    ImGui::SetNextItemWidth(w);
    ImGui::InputTextWithHint("##titlebar_search", ICON_FA_MAGNIFYING_GLASS "  Search", workspace.content.search_buf, sizeof(workspace.content.search_buf));
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    _titlebar_block(p_layout, a, b);
    return a.x;
}

void EditorApplication::_titlebar_menus()
{
    if (_topbar_menu("File")) {
        if (ImGui::MenuItem("Save", "Ctrl+S")) _save_focused();
        if (ImGui::MenuItem("Save All", "Ctrl+Shift+S", false, workspace.scene.any_dirty() || documents.any_dirty())) save_all();
        ImGui::Separator();
        if (ImGui::MenuItem("Export Project...")) ProjectExporter::run_with_dialog(project);
        ImGui::Separator();
        if (ImGui::MenuItem("Close Project")) close_project();
        if (ImGui::MenuItem("Quit", "Alt+F4")) win32.window_request_close(main_window);
        _topbar_menu_end();
    }

    if (_topbar_menu("Edit")) {
        if (ImGui::MenuItem("Editor Settings")) workspace.layout.open(workspace.editor_settings.get());
        if (ImGui::MenuItem("Project Settings")) workspace.layout.open(workspace.project_settings.get());
        ImGui::Separator();
        if (ImGui::MenuItem("Open Editor Data Folder")) Paths::reveal_in_explorer(Paths::roaming_data());
        _topbar_menu_end();
    }

    if (_topbar_menu("View")) {
        if (ImGui::BeginMenu("Theme")) {
            if (ImGui::MenuItem("Dark", nullptr, !settings.theme.light)) {
                settings.theme.light = false;
                settings.theme_pending = true;
                settings.dirty = true;
            }
            if (ImGui::MenuItem("Light", nullptr, settings.theme.light)) {
                settings.theme.light = true;
                settings.theme_pending = true;
                settings.dirty = true;
            }
            ImGui::EndMenu();
        }
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
        if (ImGui::MenuItem("Take Screenshot", "Ctrl+F12")) screenshot_request();
        _topbar_menu_end();
    }

    if (_topbar_menu("Tools")) {
        if (ImGui::MenuItem("Version Control")) workspace.layout.open(workspace.version_control.get());
        if (ImGui::MenuItem("GPU Profiler")) workspace.layout.open(&workspace.gpu_profiler);
        if (ImGui::MenuItem("Memory")) workspace.layout.open(&workspace.memory);
        _topbar_menu_end();
    }
}

void EditorApplication::_titlebar_caption_buttons(const TitlebarLayout& p_layout)
{
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const UiStyle& ui = ui_style();

    const float btns_x = p_layout.origin.x + p_layout.width - p_layout.btn_w * 3.0f;
    const float fill_h = p_layout.height - 1.0f;
    fg->AddRectFilled(ImVec2(btns_x, p_layout.origin.y), ImVec2(btns_x + p_layout.btn_w * 3.0f, p_layout.origin.y + fill_h), ui.color.topbar);

    auto client_rect = [&](float x0) -> RECT {
        return RECT{ (long)(x0 - p_layout.origin.x), 0, (long)(x0 + p_layout.btn_w - p_layout.origin.x), (long)p_layout.height };
    };

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto ctrl = [&](float x0, int glyph, bool danger) {
        const ImVec2 p(x0, p_layout.origin.y);
        const bool hovered = mouse.x >= x0 && mouse.x < x0 + p_layout.btn_w && mouse.y >= p_layout.origin.y && mouse.y < p_layout.origin.y + p_layout.height;
        if (hovered) fg->AddRectFilled(p, ImVec2(p.x + p_layout.btn_w, p.y + fill_h), danger ? ui.color.danger_strong : ui.color.topbar_hover);
        const ImVec2 c(p.x + p_layout.btn_w * 0.5f, p.y + p_layout.height * 0.5f);
        const ImU32 col = ui.color.text;
        const float s = ui.titlebar.caption_glyph;
        const float o = ui.titlebar.caption_glyph_offset;
        const float w = ui.titlebar.caption_stroke;
        switch (glyph) {
            case 0: fg->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), col, w); break;
            case 1: fg->AddRect(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, 0, 0, w); break;
            case 2:
                fg->AddRect(ImVec2(c.x - s + o, c.y - s - o), ImVec2(c.x + s + o, c.y + s - o), col, 0, 0, w);
                fg->AddRectFilled(ImVec2(c.x - s - o, c.y - s + o), ImVec2(c.x + s - o, c.y + s + o), hovered ? ui.color.topbar_hover : ui.color.topbar);
                fg->AddRect(ImVec2(c.x - s - o, c.y - s + o), ImVec2(c.x + s - o, c.y + s + o), col, 0, 0, w);
                break;
            case 3:
                fg->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, w);
                fg->AddLine(ImVec2(c.x - s, c.y + s), ImVec2(c.x + s, c.y - s), col, w);
                break;
        }
    };

    const float x_min = btns_x;
    const float x_max = btns_x + p_layout.btn_w;
    const float x_close = btns_x + p_layout.btn_w * 2.0f;
    ctrl(x_min, 0, false);
    ctrl(x_max, win32.window_is_maximized(main_window) ? 2 : 1, false);
    ctrl(x_close, 3, true);
    win32.window_titlebar_set_controls(main_window, client_rect(x_min), client_rect(x_max), client_rect(x_close));
}

void EditorApplication::_titlebar_logo(const TitlebarLayout& p_layout)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const UiStyle& ui = ui_style();
    const float side = ImFloor(p_layout.height - ui.titlebar.logo_margin * 2.0f);
    const ImVec2 mn(ImFloor(p_layout.origin.x + (p_layout.logo - side) * 0.5f), ImFloor(p_layout.origin.y + (p_layout.height - side) * 0.5f));
    const ImVec2 mx(mn.x + side, mn.y + side);
    VkDescriptorSet logo_set = imgui.texture_cache.get(resources.icon_image.image_view);
    if (logo_set) dl->AddImage(logo_set, mn, mx, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ui.color.text);
}

EditorContext& EditorApplication::_context()
{
    _ctx.render_path = dynamic_cast<EditorRenderPath*>(render_path);
    return _ctx;
}

void EditorApplication::_keep_editor_view()
{
    if (!play.game()) return;
    bool game_camera = false;
    play.runtime.view<OrbitCameraComponent>(Exclude<DisabledTag>{}, [&](Entity, const OrbitCameraComponent&) { game_camera = true; });
    if (!game_camera) play.session.camera = editor_camera.camera;
}

void EditorApplication::update_camera(float p_dt)
{
    Viewport& viewport = workspace.scene.viewport;
    if (!play.game()) editor_camera.control(viewport.camera_input, viewport.camera_view_height, p_dt);
    viewport.camera_input = {};
    editor_camera.update();
}

bool EditorApplication::renders_scene() const
{
    if (!project.loaded()) return false;
    if (const EditorDocument* w = workspace.active_document) return w->preview_scene() != nullptr;
    return workspace.scene.has_world() || workspace.scene.active_template();
}

const Camera& EditorApplication::active_camera() const
{
    if (const EditorDocument* w = workspace.active_document) {
        if (const Camera* c = w->preview_camera()) return *c;
    }
    if (!workspace.scene.active_template() && play.game()) return play.session.camera;
    return editor_camera.camera;
}

const Scene& EditorApplication::render_scene() const
{
    if (const EditorDocument* w = workspace.active_document) {
        if (const Scene* p = w->preview_scene()) return *p;
    }
    if (const TemplateDocument* doc = workspace.scene.active_template()) return doc->scene;
    if (play.active()) return play.runtime;
    return scene;
}

}
