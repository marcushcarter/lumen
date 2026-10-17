#include <editor/editor_application.h>
#include <drivers/toml/toml_helpers.h>
#include <core/io/embedded_resource.h>
#include <core/io/path.h>
#include <core/io/image_io.h>
#include <core/io/path.h>
#include <core/version.h>
#include <editor/ui/tab_strip.h>
#include <drivers/imgui/ui_style.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

#include <fstream>
#include <cstdlib>
#include <cstdint>
#include <windows.h>
#include <shellapi.h>
#include <filesystem>

namespace lumen {

Error EditorApplication::on_init()
{
    using enum Error;
    Error err;
    
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
    drawer.initialize();
    err = world_tab.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = play.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);
    err = run.initialize();
    LUMEN_ERR_FAIL_COND_V(err != OK, err);

    _load_state();
    settings.theme.apply();
    
    if (win32.window_set_titlebar_color((COLORREF)Theme::to_colorref(ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg])) != OK) log_write("Editor: custom titlebar color unsupported (needs Windows 11).");

    return OK;
}

void EditorApplication::on_shutdown()
{
    EditorContext ctx = _make_context();
    tabs.clear(ctx);

    play.shutdown();
    run.shutdown();
    resources.shutdown();
    
    _save_state();
    project_manager.save_recents();

    project_manager.shutdown();
    world_tab.shutdown();
    drawer.shutdown();
}

void EditorApplication::on_update(float p_dt)
{
    play.poll();
    run.poll();
    _draw_titlebar();

    EditorContext ctx = _make_context();
    
    if (project.loaded()) {
        imports.tick();
        if (!imports.completed.empty()) assets.request_rebuild();
        for (const auto& c : imports.completed) {
            LAssetHeader ah{};
            if (!read_asset_header(c.content_bin, ah)) continue;
            switch (ah.type) {
                case AssetType::TEXTURE:
                    if (const LTexture* old = renderer.textures.get(c.guid)) imgui.texture_cache.retire(old->image.image_view, renderer.frame_number, renderer.frame_count);
                    renderer.textures.unload(c.guid);
                    renderer.textures.load(c.guid, c.content_bin);
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
        assets.tick();
        selection.prune(world, folders);

        tabs.update(ctx, p_dt);
    } else {
        project_manager.on_update(ctx);
    }

    // popups.draw(ctx);

    if (settings.dirty && !ImGui::IsAnyItemActive()) {
        _save_state();
        settings.dirty = false;
    }
}

Error EditorApplication::open_project(const std::filesystem::path& p_root)
{
    using enum Error;
    
    Error err = project_load(p_root);
    if (err == CANCELED) return err;
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
    EditorContext ctx = _make_context();
    tabs.clear(ctx);
    tabs.add(&world_tab, WorldTab::KIND);
    tabs.focus(&world_tab);

    return OK;
}

void EditorApplication::close_project()
{
    play.stop();
    pending_transition = [this]{
        EditorContext ctx = _make_context();
        tabs.clear(ctx);
        assets.close();
        folders.clear();
        selection.clear();
        project_unload();
    };
    render_path_request(new ProjectManagerRenderPath());
}

void EditorApplication::_load_state()
{
    std::ifstream in(Paths::roaming_data() / "editor_state.cfg", std::ios::binary);
    if (!in) return;

    toml::table parsed;
    try {
        parsed = toml::parse(in);
    } catch (const toml::parse_error&) {
        return;
    }
    const toml::table& tbl = parsed;

    auto lv = tbl.at_path("layout");
    if ((int)lv["version"].value_or((int64_t)-1) == WorldTab::VERSION) {
        world_tab.layout.split_x = (float)lv["split_x"].value_or((double)world_tab.layout.split_x);
        world_tab.layout.split_y = (float)lv["split_y"].value_or((double)world_tab.layout.split_y);
        world_tab.layout.center_split = (float)lv["center_split"].value_or((double)world_tab.layout.center_split);
        world_tab.layout.drawer.collapsed = lv["center_collapsed"].value_or(world_tab.layout.drawer.collapsed);
        world_tab.layout.right_collapsed = lv["right_collapsed"].value_or(world_tab.layout.right_collapsed);
        world_tab.layout.drawer.active = (int)lv["center_tab"].value_or((int64_t)world_tab.layout.drawer.active);
        world_tab.layout.right_top.active_name = lv["active_top"].value_or(std::string{});
        world_tab.layout.right_bottom.active_name = lv["active_bottom"].value_or(std::string{});

        if (const toml::table* pans = tbl.at_path("panels").as_table()) {
            for (auto& p : world_tab.layout.panels) {
                auto pv = (*pans)[p->name()];
                p->open = pv["open"].value_or(p->open);
                p->zone = dock_zone_from_string(pv["zone"].value_or(std::string(to_string(p->zone))), p->zone);
            }
        }
    }

    settings.theme.preset = Theme::theme_preset_index(from_toml(tbl.at_path("theme.preset"), "Custom"));
    settings.theme.base = from_toml(tbl.at_path("theme.base"), settings.theme.base);
    settings.theme.accent = from_toml(tbl.at_path("theme.accent"), settings.theme.accent);
    settings.theme.text = from_toml(tbl.at_path("theme.text"), settings.theme.text);
    settings.theme.use_system_accent = tbl.at_path("theme.use_system_accent").value_or(settings.theme.use_system_accent);

    settings.vsync_mode = vsync_mode_from_name(tbl.at_path("display.vsync").value_or(std::string{}), settings.vsync_mode);
    settings.fps_cap = (int)tbl.at_path("display.fps_cap").value_or((int64_t)settings.fps_cap);
    float sp = (float)tbl.at_path("display.screen_percentage").value_or((double)world_tab.layout.viewport.screen_percentage);
    world_tab.layout.viewport.screen_percentage = sp < 0.01f ? 0.01f : (sp > 1.0f ? 1.0f : sp);

    if (auto v = tbl.at_path("window.custom_titlebar").value<bool>()) win32.window_set_custom_titlebar(*v);

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
    display.insert_or_assign("screen_percentage", (double)world_tab.layout.viewport.screen_percentage);
    
    toml::table layout;
    layout.insert_or_assign("version", (int64_t)WorldTab::VERSION);
    layout.insert_or_assign("split_x", (double)world_tab.layout.split_x);
    layout.insert_or_assign("split_y", (double)world_tab.layout.split_y);
    layout.insert_or_assign("center_split", (double)world_tab.layout.center_split);
    layout.insert_or_assign("center_collapsed", (bool)world_tab.layout.drawer.collapsed);
    layout.insert_or_assign("right_collapsed", (bool)world_tab.layout.right_collapsed);
    layout.insert_or_assign("center_tab", (int64_t)world_tab.layout.drawer.active);
    layout.insert_or_assign("active_top", world_tab.layout.right_top.active_name);
    layout.insert_or_assign("active_bottom", world_tab.layout.right_bottom.active_name);

    toml::table panels;
    for (auto& p : world_tab.layout.panels) {
        toml::table pt;
        pt.insert_or_assign("open", (bool)p->open);
        pt.insert_or_assign("zone", to_string(p->zone));
        panels.insert_or_assign(p->name(), std::move(pt));
    }

    toml::table root;
    root.insert_or_assign("theme", std::move(theme));
    root.insert_or_assign("window", std::move(window));
    root.insert_or_assign("display", std::move(display));
    root.insert_or_assign("layout", std::move(layout));
    root.insert_or_assign("panels", std::move(panels));

    std::ofstream out(Paths::roaming_data() / "editor_state.cfg", std::ios::binary);
    if (!out) return;
    out << root << '\n';
}

void EditorApplication::_titlebar_block(const TitlebarLayout& L, ImVec2 min, ImVec2 max)
{
    win32.window_titlebar_add_rect(
        (long)(min.x - L.origin.x), (long)(min.y - L.origin.y),
        (long)(max.x - L.origin.x), (long)(max.y - L.origin.y)
    );
}

void EditorApplication::_draw_titlebar()
{
    const UiStyle& ui = ui_style();
    const float TAB_H = ui.titlebar.tab_height;
    const float BTN_W = ui.titlebar.caption_width;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ui.titlebar.menu_padding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    const bool  show_tabs = project.loaded();
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

void EditorApplication::_titlebar_menus(const TitlebarLayout& L)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dock_menu_push_style();
    if (!ImGui::BeginMenuBar()) { dock_menu_pop_style(); return; }
        
    ImGui::SetCursorPosX(L.logo + ui_style().titlebar.menu_offset_x);
    float menu_x0 = ImGui::GetCursorScreenPos().x;

    if (project.loaded()) _titlebar_editor_menu();
    _titlebar_help_menu();

    float menu_x1 = ImGui::GetCursorScreenPos().x;
    if (menu_x1 > menu_x0) _titlebar_block(L, ImVec2(menu_x0, L.origin.y), ImVec2(menu_x1, L.origin.y + L.menu_h));

    const std::string& title = project.name.empty() ? std::string("Lumen Editor") : project.name;
    ImVec2 ts = ImGui::CalcTextSize(title.c_str());
    float right_pad = win32.window.custom_titlebar ? (L.width - L.btn_w * 3.0f) : L.width;
    float title_x = L.origin.x + right_pad - ts.x - ui_style().titlebar.title_margin;
    dl->AddText(ImVec2(title_x, L.origin.y + (L.menu_h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    
    if (win32.window.custom_titlebar) _titlebar_caption_buttons(L);

    ImGui::EndMenuBar();
    dock_menu_pop_style();
}

void EditorApplication::_titlebar_caption_buttons(const TitlebarLayout& L)
{
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const UiStyle& ui = ui_style();

    const float btns_x = L.origin.x + L.width - L.btn_w * 3.0f;
    fg->AddRectFilled(ImVec2(btns_x, L.origin.y), ImVec2(btns_x + L.btn_w * 3.0f, L.origin.y + L.menu_h), ImGui::GetColorU32(ImGuiCol_MenuBarBg));

    RECT rmin, rmax, rclose;
    auto client_rect = [&](float x0) -> RECT {
        return RECT{ (long)(x0 - L.origin.x), (long)(L.origin.y - L.origin.y), (long)(x0 + L.btn_w - L.origin.x), (long)(L.origin.y + L.menu_h - L.origin.y) };
    };

    ImVec2 mouse = ImGui::GetIO().MousePos;
    auto ctrl = [&](float x0, int glyph, bool danger) {
        ImVec2 p(x0, L.origin.y);
        bool hovered = mouse.x >= x0 && mouse.x < x0 + L.btn_w && mouse.y >= L.origin.y && mouse.y < L.origin.y + L.menu_h;
        if (hovered) fg->AddRectFilled(p, ImVec2(p.x + L.btn_w, p.y + L.menu_h), danger ? ui.color.danger_strong : ui.color.hover);
        ImVec2 c(p.x + L.btn_w * 0.5f, p.y + L.menu_h * 0.5f);
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
    float x_max = btns_x + L.btn_w;
    float x_close = btns_x + L.btn_w * 2.0f;

    ctrl(x_min, 0, false);
    ctrl(x_max, win32.window_is_maximized() ? 2 : 1, false);
    ctrl(x_close, 3, true);

    rmin = client_rect(x_min);
    rmax = client_rect(x_max);
    rclose = client_rect(x_close);
    win32.window_titlebar_set_controls(rmin, rmax, rclose);
}

void EditorApplication::_titlebar_tabs(const TitlebarLayout& L)
{
    const UiStyle& ui = ui_style();
    const ImVec2 min(L.origin.x + L.logo + ui.titlebar.menu_offset_x, L.origin.y + L.menu_h);
    const ImVec2 max(L.origin.x + L.width, L.origin.y + L.bar_h);
    const float stats_w = ImGui::CalcTextSize("000.00 ms  0000 fps").x + ui.titlebar.stats_margin * 2.0f;
    tabs.draw_bar(min, max, stats_w, ImGui::GetColorU32(ImGuiCol_MenuBarBg), [&](ImVec2 p_min, ImVec2 p_max) { _titlebar_block(L, p_min, p_max); });
}

void EditorApplication::_titlebar_logo(const TitlebarLayout& L)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const UiStyle& ui = ui_style();
    const float m = ui.titlebar.logo_margin;
    const ImVec2 mn(L.origin.x + m, L.origin.y + m);
    const ImVec2 mx(L.origin.x + L.logo - m, L.origin.y + L.bar_h - m);

    VkDescriptorSet logo_set = imgui.texture_cache.get(resources.icon_image.image_view);
    dl->PushClipRect(L.origin, ImVec2(L.origin.x + L.width, L.origin.y + L.bar_h), false);
    if (logo_set) dl->AddImage(logo_set, mn, mx);
    else dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_Text), ui.titlebar.logo_rounding);
    dl->PopClipRect();
}

void EditorApplication::_titlebar_help_menu()
{
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Online Documentation")) ShellExecuteA(nullptr, "open", "https://lumengames.ca", nullptr, nullptr, SW_SHOWNORMAL);
        if (ImGui::MenuItem("Forum")) ShellExecuteA(nullptr, "open", "https://lumengames.ca", nullptr, nullptr, SW_SHOWNORMAL);
        if (ImGui::MenuItem("Community")) ShellExecuteA(nullptr, "open", "https://lumengames.ca", nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::Separator();
        if (ImGui::MenuItem("Copy System Info")) {
            const auto sys = win32.get_system_info();
            const auto gpu = dd.gpu_describe();
            char buf[1024];
            std::snprintf(buf, sizeof(buf),
                "Lumen v%d.%d.%d\n"
                "OS:       %s (build %u)\n"
                "Renderer: Vulkan %s (Deferred+)\n"
                "GPU:      %s [%s]\n"
                "          %s (%s)\n"
                "          %.2f GiB VRAM\n"
                "CPU:      %s\n"
                "          %u cores / %u threads @ %u MHz\n"
                "RAM:      %.2f GiB\n"
                "Display:  %d monitor%s\n"
                "Audio:    nah\n"
                "Physics:  nah\n",
                LUMEN_VERSION_MAJOR, LUMEN_VERSION_MINOR, LUMEN_VERSION_PATCH,
                sys.os_name.c_str(), sys.os_build,
                gpu.api_version.c_str(),
                gpu.name.c_str(), gpu.type.c_str(),
                gpu.driver_name.c_str(), gpu.driver_id.c_str(),
                static_cast<double>(gpu.vram_bytes) / (1024.0 * 1024.0 * 1024.0),
                sys.cpu_brand.c_str(),
                sys.cpu_cores, sys.cpu_threads, sys.cpu_mhz,
                static_cast<double>(sys.ram_total_bytes) / (1024.0 * 1024.0 * 1024.0),
                sys.monitor_count, sys.monitor_count == 1 ? "" : "s"
            );
            ImGui::SetClipboardText(buf);
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Support Development")) ShellExecuteA(nullptr, "open", "https://lumengames.ca", nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::EndMenu();
    }
}

void EditorApplication::_titlebar_editor_menu()
{
    if (ImGui::BeginMenu("File")) {
        // search bar
        ImGui::Separator();

        // if (ImGui::MenuItem("New")) {}
        // if (ImGui::MenuItem("Open")) {}
        // if (ImGui::MenuItem("Export Scene")) {}

        ImGui::Separator();
        
        // if (ImGui::MenuItem("Open Asset")) {}

        ImGui::Separator();

        // if (ImGui::MenuItem("Save Current Scene")) {}
        // if (ImGui::MenuItem("Save Current Scene As")) {}
        if (ImGui::MenuItem("Save All")) project.save();
        // if (ImGui::MenuItem("Choose Files to Save")) {}

        ImGui::Separator();
        
        // if (ImGui::MenuItem("Import Into Scene")) {}
        // if (ImGui::MenuItem("Export All")) {}

        ImGui::Separator();
        
        // if (ImGui::MenuItem("New")) {}
        // if (ImGui::MenuItem("Open")) {}
        // if (ImGui::MenuItem("Zip Project")) {}
        if (ImGui::MenuItem("Open Current Project Directory")) Paths::reveal_in_explorer(project.root);
        // if (ImGui::MenuItem("Recent Projects")) {}

        ImGui::Separator();        

        if (ImGui::MenuItem("Exit")) close_project();
        if (ImGui::MenuItem("Quit", "Alt+F4")) win32.window_request_close();
        
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {        
        // search bar
        ImGui::Separator();
        
        // if (ImGui::MenuItem("Undo")) {}
        // if (ImGui::MenuItem("Redo")) {}
        // if (ImGui::MenuItem("Undo History")) {}
        
        ImGui::Separator();
        
        // ImGui::BeginDisabled(true);
        // if (ImGui::MenuItem("Cut")) {}
        // if (ImGui::MenuItem("Copy")) {}
        // if (ImGui::MenuItem("Paste")) {}
        // if (ImGui::MenuItem("Duplicate")) {}
        // if (ImGui::MenuItem("Delete")) {}
        // ImGui::EndDisabled();
        
        ImGui::Separator();

        // if (ImGui::MenuItem("Editor Settings")) popups.open("Editor Settings");
        // if (ImGui::MenuItem("Editor Settings")) editor.settings_popup.open();
        if (ImGui::MenuItem("Editor Settings")) tabs.open<EditorSettingsTab>();
        // if (ImGui::MenuItem("World Settings")) popups.open("World Settings");
        // if (ImGui::MenuItem("Keyboard Shortcuts")) {}
        // if (ImGui::MenuItem("Plugins")) {}

        if (ImGui::MenuItem("Open Editor Data Folder")) Paths::reveal_in_explorer(Paths::roaming_data());
        
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        // search bar
        ImGui::Separator();

        world_tab.draw_menu();
        if (ImGui::BeginMenu("Display")) {
            int vsync = (int)settings.vsync_mode;
            if (ImGui::Combo("VSync", &vsync, VSYNC_MODE_NAMES, 3)) settings.vsync_mode = (VsyncMode)vsync;
            ImGui::DragInt("FPS Cap", &settings.fps_cap, 1.0f, 0, 1000, settings.fps_cap == 0 ? "Unlimited" : "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndMenu();
        }
        
        ImGui::Separator();
        
        // if (ImGui::MenuItem("Device Output")) {}
        // if (ImGui::MenuItem("Message")) {}
        // if (ImGui::MenuItem("Output Log")) {}
        
        ImGui::Separator();

        // if (ImGui::MenuItem("Enable Fullscreen", "Alt+F11")) {}
        
        if (ImGui::MenuItem("Take Screenshot", "Ctrl+F12")) {
            if (EditorRenderPath* path = dynamic_cast<EditorRenderPath*>(render_path)) path->screenshot.requested = true;
        }
        
        ImGui::Separator();
        
        ImGui::EndMenu();
    }
    
    if (ImGui::BeginMenu("Tools")) {
        // search bar
        ImGui::Separator();
        
        // if (ImGui::MenuItem("Render Resource Viewer")) {}
        
        ImGui::Separator();

        // if (ImGui::BeginMenu("Debug")) {

        //     ImGui::EndMenu();
        // }
        
        // if (ImGui::BeginMenu("Profiler")) {

        //     ImGui::EndMenu();
        // }
        
        ImGui::EndMenu();
    }

    // if (ImGui::BeginMenu("Export")) {
    //     // search bar
    //     ImGui::Separator();
    //     
    //     // if (ImGui::MenuItem("Play In Editor Current Scene")) {}
    //     if (ImGui::MenuItem("Export")) popups.open("Export");
    //     
    //     ImGui::EndMenu();
    // }
}

EditorContext EditorApplication::_make_context()
{
    EditorContext ctx{};
    ctx.win32 = &win32;
    ctx.imgui = &imgui;
    
    ctx.renderer = &renderer;
    ctx.tasks = &tasks;
    ctx.project = &project;
    ctx.world = &world;
    
    ctx.project_manager = &project_manager;
    ctx.world_tab = &world_tab;
    ctx.drawer = &drawer;
    ctx.tabs = &tabs;
    ctx.selection = &selection;
    
    ctx.render_path = dynamic_cast<EditorRenderPath*>(render_path);
    ctx.settings = &settings;
    ctx.profiling = &profiling;
    ctx.resources = &resources;
    ctx.imports = &imports;
    ctx.assets = &assets;
    ctx.folders = &folders;

    ctx.open_project_callback = [this](const auto& path){this->open_project(path);};
    ctx.close_project_callback = [this](){this->close_project();};
    ctx.confirm_gpu_support = [this]{ return confirm_gpu_support(); };
    
    ctx.pie_is_playing = [this]{ return play.is_running(); };
    ctx.pie_toggle_play = [this]{ if (play.is_running()) play.stop(); else play.launch(project.root); };
    ctx.run_is_running = [this](const std::filesystem::path& p_root){ return run.is_running_for(p_root); };
    ctx.run_toggle = [this](const std::filesystem::path& p_root){ if (run.is_running_for(p_root)) run.stop(); else run.relaunch(p_root); };
    
    return ctx;
}

void EditorApplication::update_camera(float p_dt)
{
    // editor_camera.update(p_dt);
    
    (void)p_dt;
    editor_camera.update(0.0f);
}

const Camera& EditorApplication::active_camera() const
{
    return editor_camera.camera;
}

}