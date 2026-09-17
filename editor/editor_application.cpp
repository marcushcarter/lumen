#include <editor/editor_application.h>
#include <editor/popup/settings/editor_settings.h>
#include <editor/popup/settings/project_settings.h>
#include <editor/popup/project/new_project.h>
#include <editor/popup/project/delete_project.h>
#include <editor/popup/project/export.h>
#include <editor/popup/about/about_lumen.h>
#include <drivers/toml/toml_helpers.h>
#include <core/io/embedded_resource.h>
#include <core/io/path.h>
#include <core/io/image_io.h>
#include <core/io/path.h>
#include <core/version.h>
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
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    err = win32.window_set_icon(EmbeddedResource::load_icon(L"LUMEN_ICON"));
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    ImVec4 titlebar = ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg];
    err = win32.window_set_titlebar_color(RGB((BYTE)(titlebar.x * 255), (BYTE)(titlebar.y * 255), (BYTE)(titlebar.z * 255)));
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    
    ImGuiIO& io = ImGui::GetIO();
    {
        EmbeddedResource::Blob jb = EmbeddedResource::load(L"FONTS_JETBRAINS_MONO_REGULAR_TTF");
        ImFontConfig jb_cfg;
        jb_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)jb.data, (int)jb.size, 14.0f, &jb_cfg);

        EmbeddedResource::Blob fa = EmbeddedResource::load(L"FONTS_FA_SOLID_900_OTF");
        static const ImWchar fa_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        ImFontConfig fa_cfg;
        fa_cfg.MergeMode = true;
        fa_cfg.PixelSnapH = true;
        fa_cfg.GlyphOffset.y = 1.0f;
        fa_cfg.FontDataOwnedByAtlas = false;
        io.Fonts->AddFontFromMemoryTTF((void*)fa.data, (int)fa.size, 14.0f, &fa_cfg, fa_ranges);
        io.Fonts->Build();
    }
    
    popups.register_popup(std::make_unique<EditorSettingsPopup>());
    popups.register_popup(std::make_unique<ProjectSettingsPopup>());
    popups.register_popup(std::make_unique<ExportPopup>());
    popups.register_popup(std::make_unique<NewProjectPopup>());
    popups.register_popup(std::make_unique<DeleteProjectPopup>());
    popups.register_popup(std::make_unique<AboutLumenPopup>());

    err = project_manager.initialize();
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);
    err = editor.initialize();
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    _load_state();
    settings.theme.apply();

    return Ok;
}

void EditorApplication::on_shutdown()
{
    resources.shutdown();
    
    _save_state();
    project_manager.save_recents();

    project_manager.shutdown();
    editor.shutdown();
}

void EditorApplication::on_update(float p_dt)
{
    _draw_titlebar();

    EditorContext ctx = _make_context();
    
    popups.draw(ctx);
    if (project.loaded()) {
        imports.tick();
        for (const auto& c : imports.completed) {
            LAssetHeader ah{};
            if (!read_asset_header(c.content_bin, ah)) continue;
            switch (ah.type) {
                case AssetType::Texture:
                    renderer.textures.unload(c.guid);
                    renderer.textures.load(c.guid, c.content_bin);
                    break;
                case AssetType::Mesh:
                    renderer.geometry.unload(c.guid);
                    renderer.geometry.load(c.guid, c.content_bin);
                    break;
                default:
                    break;
            }
        }
        imports.completed.clear();

        editor.on_update(ctx, p_dt);
    } else {
        project_manager.on_update(ctx);
    }
}

Error EditorApplication::open_project(const std::filesystem::path& p_root)
{
    using enum Error;
    
    Error err = project_load(p_root);
    LUMEN_ERR_FAIL_COND_V(err != Ok, err);

    render_path_request(new EditorRenderPath());
    project_manager.add_recent(project.root, project.name);

    active_tab = 1;
    pending_tab = active_tab;

    return Ok;
}

void EditorApplication::close_project()
{
    project_unload();
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
    if ((int)lv["version"].value_or((int64_t)-1) == Editor::VERSION) {
        editor.split_x = (float)lv["split_x"].value_or((double)editor.split_x);
        editor.split_y = (float)lv["split_y"].value_or((double)editor.split_y);
        editor.center_view.split_ratio = (float)lv["center_split"].value_or((double)editor.center_view.split_ratio);
        editor.center_view.debugger.collapsed = lv["center_collapsed"].value_or(editor.center_view.debugger.collapsed);
        editor.center_view.debugger.active = (int)lv["center_tab"].value_or((int64_t)editor.center_view.debugger.active);
        editor.right_top.active_name = lv["active_top"].value_or(std::string{});
        editor.right_bottom.active_name = lv["active_bottom"].value_or(std::string{});

        if (const toml::table* pans = tbl.at_path("panels").as_table()) {
            for (auto& p : editor.panels) {
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

    if (auto v = tbl.at_path("window.custom_titlebar").value<bool>()) win32.window_set_custom_titlebar(*v);

    // viewport resolution

    renderer.graph.profiler.enabled = tbl.at_path("debugger.profiler_enabled").value_or(renderer.graph.profiler.enabled);

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
    
    toml::table viewport;
    viewport.insert_or_assign("screen_percentage", (double)editor.center_view.screen_percentage);
    
    toml::table debugger;
    debugger.insert_or_assign("profiler_enabled", static_cast<bool>(renderer.graph.profiler.enabled));

    toml::table layout;
    layout.insert_or_assign("version", (int64_t)Editor::VERSION);
    layout.insert_or_assign("split_x", (double)editor.split_x);
    layout.insert_or_assign("split_y", (double)editor.split_y);
    layout.insert_or_assign("center_split", (double)editor.center_view.split_ratio);
    layout.insert_or_assign("center_collapsed", (bool)editor.center_view.debugger.collapsed);
    layout.insert_or_assign("center_tab", (int64_t)editor.center_view.debugger.active);
    layout.insert_or_assign("active_top", editor.right_top.active_name);
    layout.insert_or_assign("active_bottom", editor.right_bottom.active_name);

    toml::table panels;
    for (auto& p : editor.panels) {
        toml::table pt;
        pt.insert_or_assign("open", (bool)p->open);
        pt.insert_or_assign("zone", to_string(p->zone));
        panels.insert_or_assign(p->name(), std::move(pt));
    }

    toml::table root;
    root.insert_or_assign("theme", std::move(theme));
    root.insert_or_assign("window", std::move(window));
    root.insert_or_assign("window", std::move(viewport));
    root.insert_or_assign("debugger", std::move(debugger));
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
    const float TAB_H = 24.0f;
    const float BTN_W = 46.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12, 7));

    const bool  show_tabs = project.loaded();
    const float MENU_H = ImGui::GetFrameHeight();
    const float H = MENU_H + TAB_H;

    ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    if (!ImGui::BeginViewportSideBar("##LumenTitlebar", ImGui::GetMainViewport(), ImGuiDir_Up, H, flags)) {
        ImGui::PopStyleVar();
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

    _titlebar_menus(L);
    if (show_tabs) _titlebar_tabs(L);
    _titlebar_logo(L);

    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorApplication::_titlebar_menus(const TitlebarLayout& L)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (!ImGui::BeginMenuBar()) return;
        
    ImGui::SetCursorPosX(L.logo + 6.0f);
    float menu_x0 = ImGui::GetCursorScreenPos().x;

    if (project.loaded()) _titlebar_editor_menu();
    _titlebar_help_menu();

    float menu_x1 = ImGui::GetCursorScreenPos().x;
    if (menu_x1 > menu_x0) _titlebar_block(L, ImVec2(menu_x0, L.origin.y), ImVec2(menu_x1, L.origin.y + L.menu_h));

    const std::string& title = project.name.empty() ? std::string("Lumen Editor") : project.name;
    ImVec2 ts = ImGui::CalcTextSize(title.c_str());
    float right_pad = win32.window.custom_titlebar ? (L.width - L.btn_w * 3.0f) : L.width;
    float title_x = L.origin.x + right_pad - ts.x - 16.0f;
    dl->AddText(ImVec2(title_x, L.origin.y + (L.menu_h - ts.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
    
    if (win32.window.custom_titlebar) _titlebar_caption_buttons(L);

    ImGui::EndMenuBar();
}

void EditorApplication::_titlebar_caption_buttons(const TitlebarLayout& L)
{
    ImDrawList* fg = ImGui::GetForegroundDrawList();

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
        if (hovered) fg->AddRectFilled(p, ImVec2(p.x + L.btn_w, p.y + L.menu_h), danger ? IM_COL32(196, 43, 28, 255) : ImGui::GetColorU32(ImGuiCol_ButtonHovered));
        ImVec2 c(p.x + L.btn_w * 0.5f, p.y + L.menu_h * 0.5f);
        ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
        float s = 5.0f;
        switch (glyph) {
            case 0: fg->AddLine(ImVec2(c.x-s,c.y), ImVec2(c.x+s,c.y), col, 1.0f); break;
            case 1: fg->AddRect(ImVec2(c.x-s,c.y-s), ImVec2(c.x+s,c.y+s), col, 0,0,1.0f); break;
            case 2:
                fg->AddRect(ImVec2(c.x-s+2,c.y-s-2), ImVec2(c.x+s+2,c.y+s-2), col, 0,0,1.0f);
                fg->AddRectFilled(ImVec2(c.x-s-2,c.y-s+2), ImVec2(c.x+s-2,c.y+s+2), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
                fg->AddRect(ImVec2(c.x-s-2,c.y-s+2), ImVec2(c.x+s-2,c.y+s+2), col, 0,0,1.0f);
                break;
            case 3:
                fg->AddLine(ImVec2(c.x-s,c.y-s), ImVec2(c.x+s,c.y+s), col, 1.2f);
                fg->AddLine(ImVec2(c.x-s,c.y+s), ImVec2(c.x+s,c.y-s), col, 1.2f);
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
    ImGui::SetCursorScreenPos(ImVec2(L.origin.x + L.logo + 6.0f, L.origin.y + L.menu_h));
    const float TAB_PAD_Y = (L.tab_h - ImGui::GetFontSize()) * 0.5f;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, TAB_PAD_Y));
    ImGui::PushStyleVar(ImGuiStyleVar_TabRounding, 4.0f);

    if (ImGui::BeginTabBar("##TitlebarTabs", ImGuiTabBarFlags_AutoSelectNewTabs)) {
        auto flags = [&](int idx) -> ImGuiTabItemFlags {
            return pending_tab == idx ? ImGuiTabItemFlags_SetSelected : 0;
        };

        // ImGui::PushID(0);
        // if (ImGui::BeginTabItem("Asset Manager", nullptr, flags(0))) { active_tab = 0; ImGui::EndTabItem(); }
        // _titlebar_block(L, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        // ImGui::PopID();

        ImGui::PushID(0);
        if (ImGui::BeginTabItem("World", nullptr, flags(1))) { active_tab = 0; ImGui::EndTabItem(); }
        _titlebar_block(L, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::PopID();

        ImGui::EndTabBar();
    }
    pending_tab = -1;
    ImGui::PopStyleVar(2);
}

void EditorApplication::_titlebar_logo(const TitlebarLayout& L)
{
    // ImDrawList* dl = ImGui::GetWindowDrawList();

    // VkDescriptorSet logo_set = imgui.texture_cache.get(resources.icon_image.image_view);
    // dl->PushClipRect(L.origin, ImVec2(L.origin.x + L.width, L.origin.y + L.bar_h), false);
    // float m = 6.0f;
    // ImVec2 mn(L.origin.x + m, L.origin.y + m);
    // ImVec2 mx(L.origin.x + L.logo - m, L.origin.y + L.bar_h - m);
    // if (logo_set) dl->AddImage(logo_set, mn, mx);
    // else dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_Text), 4.0f);
    // dl->PopClipRect();


    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float m = 6.0f;
    const ImVec2 mn(L.origin.x + m, L.origin.y + m);
    const ImVec2 mx(L.origin.x + L.logo - m, L.origin.y + L.bar_h - m);

    VkDescriptorSet logo_set = imgui.texture_cache.get(resources.icon_image.image_view);
    dl->PushClipRect(L.origin, ImVec2(L.origin.x + L.width, L.origin.y + L.bar_h), false);
    if (logo_set) dl->AddImage(logo_set, mn, mx);
    else dl->AddRectFilled(mn, mx, ImGui::GetColorU32(ImGuiCol_Text), 4.0f);
    dl->PopClipRect();
    ImGui::SetCursorScreenPos(L.origin);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.12f));
    if (ImGui::Button("##LumenLogo", ImVec2(L.logo, L.bar_h))) popups.open("About Lumen");
    ImGui::PopStyleColor(3);
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
        if (ImGui::MenuItem("About Lumen")) popups.open("About Lumen");
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

        if (ImGui::MenuItem("Editor Settings")) popups.open("Editor Settings");
        if (ImGui::MenuItem("Project Settings")) popups.open("Project Settings");
        // if (ImGui::MenuItem("Keyboard Shortcuts")) {}
        // if (ImGui::MenuItem("Plugins")) {}
        
        if (ImGui::MenuItem("Open Editor Data Folder")) Paths::reveal_in_explorer(Paths::roaming_data());
        
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        // search bar
        ImGui::Separator();

        editor.draw_menu();
        
        ImGui::Separator();
        
        // if (ImGui::MenuItem("Device Output")) {}
        // if (ImGui::MenuItem("Message")) {}
        // if (ImGui::MenuItem("Output Log")) {}
        
        ImGui::Separator();

        // if (ImGui::MenuItem("Enable Fullscreen", "Alt+F11")) {}
        
        if (ImGui::MenuItem("Take Screenshot", "Ctrl+F12")) {   
            EditorRenderPath* path = static_cast<EditorRenderPath*>(render_path);
            path->screenshot.requested = true;
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

    if (ImGui::BeginMenu("Export")) {
        // search bar
        ImGui::Separator();
        
        // if (ImGui::MenuItem("Play In Editor Current Scene")) {}
        if (ImGui::MenuItem("Export")) popups.open("Export");
        
        ImGui::EndMenu();
    }
}

EditorContext EditorApplication::_make_context()
{
    EditorContext ctx{};
    ctx.win32 = &win32;
    ctx.imgui = &imgui;
    ctx.renderer = &renderer;
    ctx.render_path = static_cast<EditorRenderPath*>(render_path);
    ctx.project = &project;
    ctx.tasks = &tasks;
    
    ctx.settings = &settings;
    ctx.resources = &resources;
    ctx.imports = &imports;
    
    ctx.project_manager = &project_manager;
    ctx.editor = &editor;
    ctx.popups = &popups;

    ctx.open_project_callback = [this](const auto& path){this->open_project(path);};
    ctx.close_project_callback = [this](){this->close_project();};
    
    ctx.pie_is_playing = [this]{ return mode == EditorMode::Play; };
    ctx.pie_toggle_play = [this]{ mode = (mode == EditorMode::Play) ? EditorMode::Edit : EditorMode::Play; paused = false; renderer.camera_cut(); };
    ctx.pie_is_paused = [this]{ return paused; };
    ctx.pie_toggle_pause = [this]{ paused = !paused; };
    
    return ctx;
}

void EditorApplication::update_camera(float p_dt)
{
    if (mode == EditorMode::Edit) editor_camera.update(p_dt);
    if (mode == EditorMode::Play) {
        
    }
}

const Camera& EditorApplication::active_camera() const
{
    if (mode == EditorMode::Edit) return editor_camera.camera;
    return *world.active_camera;
}

}