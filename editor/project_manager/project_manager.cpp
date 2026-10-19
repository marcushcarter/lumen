#include <editor/project_manager/project_manager.h>
#include <drivers/imgui/ui_style.h>
#include <drivers/windows/dialogs_win32.h>
#include <drivers/toml/toml_helpers.h>
#include <core/project/project.h>
#include <core/io/path.h>
#include <core/base/error.h>
#include <core/base/utils.h>
#include <drivers/imgui/tab_strip.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <cfloat>
#include <cstdio>

#include <fstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <numbers>

namespace lumen {

Error ProjectManager::initialize()
{
    using enum Error;
    load_recents();
    return OK;
}

void ProjectManager::shutdown()
{
    save_recents();
}

void ProjectManager::load_recents()
{
    recent.clear();

    std::ifstream in(Paths::local_data() / "recents", std::ios::binary);
    if (!in) return;

    toml::table parsed;
    try {
        parsed = toml::parse(in);
    } catch (const toml::parse_error&) {
        return;
    }

    const toml::array* entries = parsed.at_path("recent").as_array();
    if (!entries) return;

    for (const toml::node& node : *entries) {
        const toml::table* t = node.as_table();
        if (!t) continue;

        auto path_str = (*t)["path"].value<std::string>();
        if (!path_str) continue;

        std::filesystem::path path = *path_str;
        std::error_code ec;

        Entry e;
        e.path = path;
        e.missing = !std::filesystem::exists(path / Project::FILE_NAME, ec);
        e.name = e.missing ? (*t)["name"].value_or(path.filename().string()) : Project::peek_name(path);
        e.favorite = (*t)["favorite"].value_or(false);
        e.last_opened = (*t)["last_opened"].value_or(std::string{});
        recent.push_back(std::move(e));
    }
}
 
void ProjectManager::save_recents()
{   
    toml::array entries;
    for (const auto& e : recent) {
        toml::table t;
        t.insert_or_assign("path", e.path.string());
        t.insert_or_assign("name", e.name);
        t.insert_or_assign("favorite", e.favorite);
        t.insert_or_assign("last_opened", e.last_opened);
        entries.push_back(std::move(t));
    }

    toml::table root;
    root.insert_or_assign("recent", std::move(entries));

    std::ofstream out(Paths::local_data() / "recents", std::ios::binary);
    if (!out) return;
    out << root << '\n';
}
 
void ProjectManager::add_recent(const std::filesystem::path& p_root, std::string_view p_name)
{
    bool was_fav = false;
    for (size_t i = 0; i < recent.size(); i++) {
        if (recent[i].path == p_root) {
            was_fav = recent[i].favorite;
            recent.erase(recent.begin() + i);
            break;
        }
    }
    
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);

    Entry e;
    e.name = std::string(p_name);
    e.path = p_root;
    e.favorite = was_fav;
    e.last_opened = buf;
    recent.insert(recent.begin(), std::move(e));
    save_recents();
}

static uint32_t _name_hash(std::string_view p_name)
{
    uint32_t h = 2166136261u;
    for (char c : p_name) { h ^= (uint8_t)c; h *= 16777619u; }
    return h;
}

static void _draw_placeholder_thumb(ImDrawList* dl, ImVec2 p_min, ImVec2 p_max, std::string_view p_name, float p_rounding, float p_font_size)
{
    const float hue = (float)(_name_hash(p_name) % 360) / 360.0f;
    dl->AddRectFilled(p_min, p_max, ImGui::GetColorU32((ImVec4)ImColor::HSV(hue, 0.35f, 0.42f)), p_rounding);

    char initials[3] = {};
    int n = 0;
    bool word_start = true;
    for (char c : p_name) {
        if (c == '_' || c == ' ' || c == '-') { word_start = true; continue; }
        if (word_start && n < 2) initials[n++] = (char)std::toupper((unsigned char)c);
        word_start = false;
    }

    const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(p_font_size, FLT_MAX, 0.0f, initials);
    const ImVec2 pos(ImFloor((p_min.x + p_max.x - ts.x) * 0.5f), ImFloor((p_min.y + p_max.y - ts.y) * 0.5f));
    dl->AddText(ImGui::GetFont(), p_font_size, pos, ui_style().color.text, initials);
}

static bool _star_button(const char* p_id, ImVec2 p_center, float p_radius, bool p_on)
{
    const UiStyle& ui = ui_style();
    ImGui::SetCursorScreenPos(ImVec2(p_center.x - p_radius, p_center.y - p_radius));
    const bool pressed = ImGui::InvisibleButton(p_id, ImVec2(p_radius * 2.0f, p_radius * 2.0f));
    const bool hovered = ImGui::IsItemHovered();
    const ImU32 col = p_on ? ui.color.favorite : (hovered ? ui.color.text : ui.color.text_dim);

    ImVec2 pts[10];
    for (int i = 0; i < 10; ++i) {
        const float ang = -std::numbers::pi_v<float> * 0.5f + i * (std::numbers::pi_v<float> / 5.0f);
        const float rad = (i & 1) ? p_radius * 0.5f : p_radius;
        pts[i] = ImVec2(p_center.x + std::cos(ang) * rad, p_center.y + std::sin(ang) * rad);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (p_on) dl->AddConcavePolyFilled(pts, 10, col);
    else dl->AddPolyline(pts, 10, col, ImDrawFlags_Closed, ui.list_row.star_stroke);
    return pressed;
}

static bool _chip(const char* p_label, bool p_on)
{
    const UiStyle& ui = ui_style();
    const char* end = ImGui::FindRenderedTextEnd(p_label);
    const ImVec2 ts = ImGui::CalcTextSize(p_label, end);
    const ImVec2 size(ts.x + ui.project_manager.chip_pad_x * 2.0f, ImGui::GetFrameHeight());
    const ImVec2 p = ImGui::GetCursorScreenPos();

    const bool pressed = ImGui::InvisibleButton(p_label, size);
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + size.x, p.y + size.y);
    const ImU32 bg = p_on ? ui.color.accent_soft : (hovered ? ui.color.field_hover : ui.color.field);
    dl->AddRectFilled(p, q, bg, ui.radius.pill);
    dl->AddRect(p, q, p_on ? ui.color.accent_soft_active : ui.color.line, ui.radius.pill, 0, ui.stroke.hairline);
    dl->AddText(ImVec2(ImFloor(p.x + ui.project_manager.chip_pad_x), ImFloor(p.y + (size.y - ts.y) * 0.5f)), (p_on || hovered) ? ui.color.text : ui.color.text_dim, p_label, end);
    return pressed;
}

static void _push_primary_button()
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_Button, ui.color.accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.accent_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.accent_active);
    ImGui::PushStyleColor(ImGuiCol_Text, ui.color.text);
}

static void _push_run_button()
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_Button, ui.color.success_soft);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.success_soft_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.success_soft_active);
    ImGui::PushStyleColor(ImGuiCol_Text, ui.color.success_text);
}

static void _push_neutral_button()
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_Button, ui.color.field);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.field_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.field_active);
    ImGui::PushStyleColor(ImGuiCol_Text, ui.color.text);
}

static float _button_width(const char* p_label)
{
    return ImGui::CalcTextSize(p_label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

ProjectManager::ContentStats ProjectManager::_scan_content(const std::filesystem::path& p_root)
{
    ContentStats s;
    std::error_code ec;
    const auto opts = std::filesystem::directory_options::skip_permission_denied;

    for (auto it = std::filesystem::recursive_directory_iterator(p_root, opts, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        const uintmax_t size = it->file_size(fec);
        if (!fec) s.disk_bytes += size;
    }

    ec.clear();
    for (auto it = std::filesystem::recursive_directory_iterator(p_root / Project::DIR_ASSETS, opts, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        const std::filesystem::path ext = it->path().extension();
        if (ext == ".lmesh") s.meshes++;
        else if (ext == ".ltexture") s.textures++;
        else if (ext == ".lmaterial") s.materials++;
        else if (ext == ".lmodel") s.models++;
    }
    return s;
}

void ProjectManager::_update_stats()
{
    if (stats_job.valid()) {
        if (stats_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        const ContentStats s = stats_job.get();
        for (Entry& e : recent) {
            if (e.path != stats_job_path) continue;
            e.stats = s;
            e.stats_valid = true;
        }
        stats_job_path.clear();
    }

    if (selected < 0 || selected >= (int)recent.size()) return;
    const Entry& e = recent[selected];
    if (e.stats_valid || e.missing) return;

    stats_job_path = e.path;
    stats_job = std::async(std::launch::async, &ProjectManager::_scan_content, e.path);
}

void ProjectManager::_rebuild_visible()
{
    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };
    const std::string query = to_lower(filter_buf);

    visible.clear();
    for (int i = 0; i < (int)recent.size(); ++i) {
        if (filter == Filter::FAVORITES && !recent[i].favorite) continue;
        if (filter == Filter::MISSING && !recent[i].missing) continue;
        if (!query.empty() && to_lower(recent[i].name).find(query) == std::string::npos && to_lower(recent[i].path.string()).find(query) == std::string::npos) continue;
        visible.push_back(i);
    }
    std::stable_sort(visible.begin(), visible.end(), [&](int a, int b) {
        if (recent[a].favorite != recent[b].favorite) return recent[a].favorite > recent[b].favorite;
        if (sort_index == 0) return recent[a].last_opened > recent[b].last_opened;
        if (sort_index == 1) return recent[a].name < recent[b].name;
        if (sort_index == 2) return recent[a].path < recent[b].path;
        return false;
    });
}

void ProjectManager::_draw_toolbar()
{
    const UiStyle& ui = ui_style();
    const UiStyle::ProjectManager& pm = ui.project_manager;
    const float sp = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(wp.x, wp.y + ws.y - ui.stroke.hairline), ImVec2(wp.x + ws.x, wp.y + ws.y), ui.color.line);

    const float row_y = ImFloor((ws.y - ui.stroke.hairline - ImGui::GetFrameHeight()) * 0.5f);
    ImGui::SetCursorPos(ImVec2(ui.toolbar.padding_x, row_y));
    dock_search_bar("##filter", filter_buf, sizeof(filter_buf), pm.filter_width);

    int favorite_count = 0;
    int missing_count = 0;
    for (const Entry& e : recent) {
        favorite_count += e.favorite ? 1 : 0;
        missing_count += e.missing ? 1 : 0;
    }
    if (missing_count == 0 && filter == Filter::MISSING) filter = Filter::ALL;
    char all_label[32];
    char fav_label[48];
    snprintf(all_label, sizeof(all_label), "All %d###chip_all", (int)recent.size());
    snprintf(fav_label, sizeof(fav_label), ICON_FA_STAR " Favorites %d###chip_fav", favorite_count);

    ImGui::SameLine();
    if (_chip(all_label, filter == Filter::ALL)) filter = Filter::ALL;
    ImGui::SameLine();
    if (_chip(fav_label, filter == Filter::FAVORITES)) filter = Filter::FAVORITES;
    if (missing_count > 0) {
        char missing_label[48];
        snprintf(missing_label, sizeof(missing_label), ICON_FA_TRIANGLE_EXCLAMATION " Missing %d###chip_missing", missing_count);
        ImGui::SameLine();
        if (_chip(missing_label, filter == Filter::MISSING)) filter = Filter::MISSING;
    }

    const char* add_label = "Add Existing...";
    const char* new_label = ICON_FA_PLUS "  New Project";
    const float right_w = ImGui::CalcTextSize("Sort").x + sp + pm.sort_width + sp + _button_width(add_label) + sp + _button_width(new_label);
    ImGui::SameLine(ws.x - ui.toolbar.padding_x - right_w);

    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Sort");
    ImGui::SameLine();
    const char* sorts[] = { "Last Opened", "Name", "Path" };
    ImGui::SetNextItemWidth(pm.sort_width);
    dock_field_push_style();
    ImGui::Combo("##sort", &sort_index, sorts, 3);
    dock_field_pop_style();

    ImGui::SameLine();
    _push_neutral_button();
    ImGui::Button(add_label);
    ImGui::PopStyleColor(4);

    ImGui::SameLine();
    _push_primary_button();
    if (ImGui::Button(new_label)) new_project_request = true;
    ImGui::PopStyleColor(4);
}

void ProjectManager::_draw_list(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    const UiStyle::ProjectManager& pm = ui.project_manager;

    if (visible.empty()) {
        const char* msg = recent.empty() ? "No projects yet." : "No projects found.";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - ts.x) * 0.5f, (ImGui::GetWindowHeight() - ts.y) * 0.5f));
        ImGui::TextDisabled("%s", msg);
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool hoverable = ImGui::IsWindowHovered();
    const float row_w = ImGui::GetContentRegionAvail().x;
    const float star_r = ImGui::GetTextLineHeight() * 0.5f;
    const float line_h = ImGui::GetTextLineHeight();

    for (int idx : visible) {
        Entry& e = recent[idx];
        const bool sel = selected == idx;

        const ImVec2 rmin = ImGui::GetCursorScreenPos();
        const ImVec2 rmax(rmin.x + row_w, rmin.y + pm.row_height);
        const bool hovered = hoverable && ImGui::IsMouseHoveringRect(rmin, rmax);

        if (sel) {
            dl->AddRectFilled(rmin, rmax, ui.color.accent_soft);
            dl->AddRectFilled(rmin, ImVec2(rmin.x + pm.row_accent_bar, rmax.y), ui.color.accent);
        } else if (hovered) {
            dl->AddRectFilled(rmin, rmax, ui.color.hover);
        }
        dl->AddRectFilled(ImVec2(rmin.x, rmax.y - ui.stroke.hairline), rmax, ui.color.line);

        ImGui::PushID(idx);

        float x = rmin.x + pm.row_pad_x;
        const float cy = rmin.y + pm.row_height * 0.5f;
        if (_star_button("##fav", ImVec2(x + star_r, cy), star_r, e.favorite)) {
            e.favorite = !e.favorite;
            save_recents();
        }
        x += star_r * 2.0f + pm.row_gap;

        const ImVec2 tmin(x, ImFloor(cy - pm.thumb_size * 0.5f));
        const ImVec2 tmax(tmin.x + pm.thumb_size, tmin.y + pm.thumb_size);
        _draw_placeholder_thumb(dl, tmin, tmax, e.name, pm.thumb_rounding, ImGui::GetFontSize());
        if (e.missing) dl->AddRectFilled(tmin, tmax, ui_with_alpha(ui.color.chrome, ui.alpha.overlay_active), pm.thumb_rounding);
        x += pm.thumb_size + pm.row_gap;

        ImGui::SetCursorScreenPos(ImVec2(rmin.x + pm.row_pad_x + star_r * 2.0f + pm.row_gap * 0.5f, rmin.y));
        if (ImGui::InvisibleButton("##row", ImVec2(rmax.x - ImGui::GetCursorScreenPos().x, pm.row_height))) selected = idx;
        if (!e.missing && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ctx.open_project_callback(e.path);

        ImGui::PopID();

        const float sub_size = ImGui::GetFontSize() * ui.list_row.subtitle_scale;
        const float text_h = line_h + ui.list_row.subtitle_gap + sub_size;
        const float ty = ImFloor(cy - text_h * 0.5f);
        dl->AddText(ImVec2(x, ty), e.missing ? ui.color.text_dim : ui.color.text, e.name.c_str());
        const std::string path_str = e.missing ? e.path.string() + "  -  not found" : e.path.string();
        dl->AddText(ImGui::GetFont(), sub_size, ImVec2(x, ty + line_h + ui.list_row.subtitle_gap), e.missing ? ui.color.danger : ui.color.text_dim, path_str.c_str());
        if (!e.last_opened.empty()) {
            const float mw = ImGui::CalcTextSize(e.last_opened.c_str()).x;
            dl->AddText(ImVec2(rmax.x - mw - pm.row_pad_x, ImFloor(cy - line_h * 0.5f)), ui.color.text_dim, e.last_opened.c_str());
        }

        ImGui::SetCursorScreenPos(ImVec2(rmin.x, rmax.y));
    }
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered()) selected = -1;
}

void ProjectManager::_draw_inspector(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    const UiStyle::ProjectManager& pm = ui.project_manager;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    const ImVec2 ws = ImGui::GetWindowSize();

    if (selected < 0 || selected >= (int)recent.size()) {
        const char* msg = "Select a project";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        ImGui::SetCursorPos(ImVec2((ws.x - ts.x) * 0.5f, (ws.y - ts.y) * 0.5f));
        ImGui::TextDisabled("%s", msg);
        return;
    }
    Entry& e = recent[selected];

    _draw_placeholder_thumb(dl, wp, ImVec2(wp.x + ws.x, wp.y + pm.hero_height), e.name, 0.0f, pm.title_font_size * 2.0f);

    ImGui::SetCursorPos(ImVec2(0.0f, pm.hero_height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pm.inspector_padding);
    ImGui::BeginChild("##inspector_body", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();

    const float avail = ImGui::GetContentRegionAvail().x;
    const ImVec2 title_pos = ImGui::GetCursorScreenPos();
    ImGui::PushFont(nullptr, pm.title_font_size);
    ImGui::TextUnformatted(e.name.c_str());
    const float title_h = ImGui::GetItemRectSize().y;
    ImGui::PopFont();
    if (e.last_opened.empty()) ImGui::TextDisabled("Never opened");
    else ImGui::TextDisabled("Last opened %s", e.last_opened.c_str());
    if (e.missing) {
        ImGui::Dummy(ImVec2(0.0f, ui.space.sm));
        ImGui::PushStyleColor(ImGuiCol_Text, ui.color.danger);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(ICON_FA_TRIANGLE_EXCLAMATION "  Project folder not found. It may have been moved or deleted.");
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    const float star_r = ImGui::GetTextLineHeight() * 0.6f;
    const ImVec2 after = ImGui::GetCursorScreenPos();
    if (_star_button("##fav_inspector", ImVec2(title_pos.x + avail - star_r, title_pos.y + title_h * 0.5f + ui.space.sm), star_r, e.favorite)) {
        e.favorite = !e.favorite;
        save_recents();
    }
    ImGui::SetCursorScreenPos(after);

    ImGui::Dummy(ImVec2(0.0f, ui.space.lg));
    ImGui::TextDisabled("CONTENT");
    ImGui::Dummy(ImVec2(0.0f, ui.space.xxs));

    auto content_row = [&](const char* p_label, const char* p_value) {
        ImGui::TextDisabled("%s", p_label);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(p_value).x);
        ImGui::TextUnformatted(p_value);
    };
    char meshes[16] = "...";
    char textures[16] = "...";
    char materials[16] = "...";
    char models[16] = "...";
    const char* disk = "...";
    if (e.missing) {
        snprintf(meshes, sizeof(meshes), "-");
        snprintf(textures, sizeof(textures), "-");
        snprintf(materials, sizeof(materials), "-");
        snprintf(models, sizeof(models), "-");
        disk = "-";
    } else if (e.stats_valid) {
        snprintf(meshes, sizeof(meshes), "%u", e.stats.meshes);
        snprintf(textures, sizeof(textures), "%u", e.stats.textures);
        snprintf(materials, sizeof(materials), "%u", e.stats.materials);
        snprintf(models, sizeof(models), "%u", e.stats.models);
        disk = fmt_bytes(e.stats.disk_bytes);
    }
    content_row("Meshes", meshes);
    content_row("Textures", textures);
    content_row("Materials", materials);
    content_row("Models", models);
    content_row("Disk", disk);

    ImGui::Dummy(ImVec2(0.0f, ui.space.xxl));

    const float full = ImGui::GetContentRegionAvail().x;
    const float half = ImFloor((full - pm.action_gap) * 0.5f);
    const bool missing = e.missing;

    ImGui::BeginDisabled(missing);
    _push_primary_button();
    if (ImGui::Button("Open in Editor", ImVec2(full, pm.action_height))) ctx.open_project_callback(e.path);
    ImGui::PopStyleColor(4);

    ImGui::Dummy(ImVec2(0.0f, pm.action_gap - ImGui::GetStyle().ItemSpacing.y));
    const bool playing = ctx.run_is_running && ctx.run_is_running(e.path);
    if (playing) _push_neutral_button();
    else _push_run_button();
    if (ImGui::Button(playing ? ICON_FA_STOP "  Stop" : ICON_FA_PLAY "  Run", ImVec2(full, pm.action_height)) && ctx.run_toggle) {
        if (playing || (ctx.confirm_gpu_support && ctx.confirm_gpu_support())) ctx.run_toggle(e.path);
    }
    ImGui::PopStyleColor(4);
    ImGui::EndDisabled();

    ImGui::Dummy(ImVec2(0.0f, pm.action_gap - ImGui::GetStyle().ItemSpacing.y));
    _push_neutral_button();
    if (ImGui::Button("Remove", ImVec2(half, pm.action_height))) {
        recent.erase(recent.begin() + selected);
        save_recents();
        selected = -1;
    }
    ImGui::SameLine(0.0f, pm.action_gap);
    ImGui::BeginDisabled(missing);
    ImGui::Button("Delete", ImVec2(full - half - pm.action_gap, pm.action_height));
    ImGui::EndDisabled();
    ImGui::PopStyleColor(4);

    ImGui::EndChild();
}

void ProjectManager::on_update(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    const UiStyle::ProjectManager& pm = ui.project_manager;
    ImGuiViewport* vp = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ui.color.sunken);
    ImGui::Begin("ProjectManager", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    _rebuild_visible();
    _update_stats();

    const float gap = ui.dock.gap;
    const float bar_h = ui.toolbar.height;
    const ImVec2 size = ImGui::GetContentRegionAvail();
    const float body_y = bar_h + gap;
    const float body_h = ImMax(1.0f, size.y - body_y);
    const float list_w = ImMax(1.0f, size.x - pm.inspector_width - gap);
    const ImGuiWindowFlags block_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::SetCursorPos(ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.chrome);
    ImGui::BeginChild("##projects_toolbar", ImVec2(size.x, bar_h), ImGuiChildFlags_None, block_flags);
    _draw_toolbar();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(0.0f, body_y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.chrome);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##projects_list", ImVec2(list_w, body_h));
    _draw_list(ctx);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.panel);

    ImGui::SetCursorPos(ImVec2(list_w + gap, body_y));
    ImGui::BeginChild("##projects_inspector", ImVec2(pm.inspector_width, body_h), ImGuiChildFlags_None, block_flags);
    _draw_inspector(ctx);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::End();

    if (new_project_request) {
        ImGui::OpenPopup("New Project");
        new_project_request = false;
    }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ui.project_manager.modal_width, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        const float browse_w = ui.project_manager.browse_width;

        ImGui::InputText("Name", new_project_name, sizeof(new_project_name));

        ImGui::SetNextItemWidth(ImGui::CalcItemWidth() - browse_w - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##location", new_project_location, sizeof(new_project_location));
        ImGui::SameLine();
        if (ImGui::Button("Browse", ImVec2(browse_w, 0))) {
            const std::wstring picked = drivers::Win32Dialogs::open_folder(L"Choose project location");
            if (!picked.empty()) std::snprintf(new_project_location, sizeof(new_project_location), "%s", std::filesystem::path(picked).string().c_str());
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("Location");

        const std::filesystem::path root = std::filesystem::path(new_project_location) / new_project_name;
        std::error_code ec;
        const bool has_name = new_project_name[0] != '\0';
        const bool has_location = std::filesystem::is_directory(new_project_location, ec);
        const bool taken = has_name && has_location && std::filesystem::exists(root / Project::FILE_NAME, ec);

        if (!has_name) ImGui::TextDisabled("Enter a project name");
        else if (!has_location) ImGui::TextDisabled("Choose a location that exists");
        else if (taken) ImGui::TextColored(ui_vec4(ui.color.danger), "That folder already contains a project");
        else ImGui::TextDisabled("%s", root.string().c_str());

        ImGui::Spacing();
        ImGui::BeginDisabled(!has_name || !has_location || taken);
        if (ImGui::Button("Create", ImVec2(ui.project_manager.button_width, 0)) && ctx.confirm_gpu_support() && Project::create(root, new_project_name) == Error::OK) {
            add_recent(root, new_project_name);
            ImGui::CloseCurrentPopup();
            ctx.open_project_callback(root);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(ui.project_manager.button_width, 0))) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

}