#include <editor/project_manager/project_manager.h>
// #include <editor/popup/popup_manager.h>
// #include <editor/popup/project/delete_project.h>
#include <drivers/windows/dialogs_win32.h>
#include <drivers/toml/toml_helpers.h>
#include <core/project/project.h>
#include <core/io/path.h>
#include <core/base/error.h>
#include <imgui.h>

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
        if (!std::filesystem::exists(path / Project::FILE_NAME)) continue;

        Entry e;
        e.path = path;
        e.name = Project::peek_name(path);
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

void ProjectManager::_draw_list(EditorContext& ctx)
{
    const float row_h = 48.0f;
    const float row_pad = 4.0f;
    const float rounding = 5.0f;
    const float pad = 8.0f;

    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    };
    std::string filter = to_lower(filter_buf);

    std::vector<int> order;
    order.reserve(recent.size());
    for (int i = 0; i < (int)recent.size(); ++i) {
        if (!filter.empty() && to_lower(recent[i].name).find(filter) == std::string::npos && to_lower(recent[i].path.string()).find(filter) == std::string::npos) continue;
        order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (recent[a].favorite != recent[b].favorite) return recent[a].favorite > recent[b].favorite;
        if (sort_index == 0) return recent[a].last_opened > recent[b].last_opened;
        if (sort_index == 1) return recent[a].name < recent[b].name;
        if (sort_index == 2) return recent[a].path < recent[b].path;
        return false;
    });

    if (order.empty()) {
        const char* msg = recent.empty() ? "No projects yet." : "No projects found.";
        float tw = ImGui::CalcTextSize(msg).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - tw) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f);
        ImGui::TextDisabled("%s", msg);
        return;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();

    bool hoverable = ImGui::IsWindowHovered();

    for (int idx : order) {
        Entry& e = recent[idx];

        ImGui::Dummy(ImVec2(0, row_pad));
        bool sel = (selected == idx);
        ImVec2 rmin = ImGui::GetCursorScreenPos();
        float availw = ImGui::GetContentRegionAvail().x;
        ImVec2 rmax = ImVec2(rmin.x + availw, rmin.y + row_h);

        if (sel) dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImGuiCol_Button), rounding);
        else if (hoverable && ImGui::IsMouseHoveringRect(rmin, rmax)) dl->AddRectFilled(rmin, rmax, ImGui::GetColorU32(ImGuiCol_ButtonHovered), rounding);

        dl->AddLine(ImVec2(rmin.x + pad, rmax.y), ImVec2(rmax.x - pad, rmax.y), ImGui::GetColorU32(ImGuiCol_Button));

        float thumb = row_h - pad * 2;
        float thumb_x = rmin.x + pad * 4;
        float thumb_y = rmin.y + pad;
        dl->AddRectFilled(ImVec2(thumb_x, thumb_y), ImVec2(thumb_x + thumb, thumb_y + thumb), ImGui::GetColorU32(ImGuiCol_TextDisabled), 4.0f);
        dl->AddRect(ImVec2(thumb_x, thumb_y), ImVec2(thumb_x + thumb, thumb_y + thumb), ImGui::GetColorU32(ImGuiCol_Text), 4.0f);

        ImGui::PushID(idx);

        // Star.
        {
            float star_r = ImGui::GetTextLineHeight() * 0.5f;
            ImVec2 star_c = ImVec2(rmin.x + pad + star_r, rmin.y + row_h * 0.5f);
            ImGui::SetCursorScreenPos(ImVec2(star_c.x - star_r, star_c.y - star_r));
            if (ImGui::InvisibleButton("##fav", ImVec2(star_r * 2, star_r * 2))) { e.favorite = !e.favorite; save_recents(); }
            bool star_hover = ImGui::IsItemHovered();
            ImU32 star_col = e.favorite ? IM_COL32(255, 180, 0, 255) : star_hover ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            
            ImVec2 pts[10];
            for (int i = 0; i < 10; ++i) {
                float ang = -std::numbers::pi_v<float> * 0.5f + i * (std::numbers::pi_v<float> / 5.0f);
                float rad = (i & 1) ? star_r * 0.5f : star_r;
                pts[i] = ImVec2(star_c.x + std::cos(ang) * rad, star_c.y + std::sin(ang) * rad);
            }
            if (e.favorite) dl->AddConcavePolyFilled(pts, 10, star_col);
            else dl->AddPolyline(pts, 10, star_col, ImDrawFlags_Closed, 1.5f);
        }

        ImGui::SetCursorScreenPos(ImVec2(thumb_x, rmin.y));
        if (ImGui::InvisibleButton("##row", ImVec2(rmax.x - thumb_x, row_h))) selected = idx;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) ctx.open_project_callback(e.path);

        ImGui::PopID();

        float text_x = thumb_x + thumb + pad;
        dl->AddText(ImVec2(text_x, thumb_y), ImGui::GetColorU32(ImGuiCol_Text), e.name.c_str());
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.85f, ImVec2(text_x, thumb_y + ImGui::GetTextLineHeight() + 3.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), e.path.string().c_str());
        if (!e.last_opened.empty()) {
            float mw = ImGui::CalcTextSize(e.last_opened.c_str()).x;
            dl->AddText(ImVec2(rmax.x - mw - pad, rmin.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), e.last_opened.c_str());
        }
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && ImGui::IsWindowHovered()) selected = -1;

    ImGui::Dummy(ImVec2(0, row_pad));
    ImGui::PopStyleVar();
}
// editor/project_manager/project_manager.cpp, line 215
void ProjectManager::on_update(EditorContext& ctx)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("ProjectManager", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);
    
    // if (ImGui::Button("New Project")) ctx.popups->open("New Project");
    // if (ImGui::Button("New Project")) new_project_popup.open();
    if (ImGui::Button("New Project")) new_project_request = true;
    ImGui::SameLine();

    const float sort_w = 160.0f;
    float sort_label_w = ImGui::CalcTextSize("Sort:").x + ImGui::GetStyle().ItemSpacing.x;
    float filter_w = ImGui::GetContentRegionAvail().x - sort_w - sort_label_w - ImGui::GetStyle().ItemSpacing.x * 2.0f;
    ImGui::SetNextItemWidth(filter_w);
    ImGui::InputTextWithHint("##filter", "Filter projects", filter_buf, sizeof(filter_buf));
    ImGui::SameLine();
    ImGui::TextUnformatted("Sort:");
    ImGui::SameLine();
    const char* sorts[] = { "Last Edited", "Name", "Path" };
    ImGui::SetNextItemWidth(sort_w);
    ImGui::Combo("##sort", &sort_index, sorts, 3);

    ImGui::Spacing();

    const float actions_w = 150.0f;
    float list_w = ImGui::GetContentRegionAvail().x - actions_w - ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginChild("list", ImVec2(list_w, 0), ImGuiChildFlags_Borders);
    _draw_list(ctx);
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("actions", ImVec2(actions_w, 0));
    bool has_sel = (selected >= 0 && selected < (int)recent.size());
    ImGui::BeginDisabled(!has_sel);

    if (ImGui::Button("Open", ImVec2(-1, 0)))
        ctx.open_project_callback(recent[selected].path);

    if (ImGui::Button("Remove", ImVec2(-1, 0))) {
        recent.erase(recent.begin() + selected);
        save_recents();
        selected = -1;
    }

    // if (ImGui::Button("Delete", ImVec2(-1, 0))) {
    //     if (auto* p = ctx.popups->get<DeleteProjectPopup>("Delete Project")) {
    //         p->project_path = recent[selected].path;
    //         p->project_name = recent[selected].name;
    //     }
    //     ctx.popups->open("Delete Project");
    // }
    // if (ImGui::Button("Delete", ImVec2(-1, 0))) delete_project_popup.open(recent[selected].path, recent[selected].name);

    ImGui::BeginDisabled(true);
    if (ImGui::Button("Export", ImVec2(-1, 0))) {}
    if (ImGui::Button("Run", ImVec2(-1, 0))) {}
    ImGui::EndDisabled();

    ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::End();

    if (new_project_request) {
        ImGui::OpenPopup("New Project");
        new_project_request = false;
    }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        const float browse_w = 90.0f;

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
        else if (taken) ImGui::TextColored(ImVec4(0.86f, 0.35f, 0.35f, 1.0f), "That folder already contains a project");
        else ImGui::TextDisabled("%s", root.string().c_str());

        ImGui::Spacing();
        ImGui::BeginDisabled(!has_name || !has_location || taken);
        if (ImGui::Button("Create", ImVec2(120.0f, 0)) && Project::create(root, new_project_name) == Error::OK) {
            add_recent(root, new_project_name);
            ImGui::CloseCurrentPopup();
            ctx.open_project_callback(root);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0))) ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    // new_project_popup.draw(ctx);
    // delete_project_popup.draw(ctx);
}
}
