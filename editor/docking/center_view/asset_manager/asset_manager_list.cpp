#include <editor/docking/center_view/asset_manager/asset_manager_list.h>
#include <core/rendering/renderer.h>
#include <editor/editor_resources.h>
#include <drivers/imgui/imgui_driver.h>
#include <core/project/project.h>
#include <editor/assets/asset_import_tracker.h>
#include <core/io/path.h>
#include <imgui.h>
#include <cstdio>

namespace lumen {

void AssetBrowserList::_delete_content(EditorContext& ctx, const std::filesystem::path& p_asset)
{
    AssetInfo info = read_asset_info(p_asset);
    if (!info.valid()) return;

    if (ctx.renderer) {
        switch (info.type) {
            case AssetType::Texture: ctx.renderer->textures.unload(info.guid); break;
            case AssetType::Mesh: ctx.renderer->geometry.unload(info.guid); break;
            default: break;
        }
    }

    Paths::remove_to_recycle(ctx.project->content_path(info.guid));
}

void AssetBrowserList::_delete_asset(EditorContext& ctx, const std::filesystem::path& p_path)
{
    _delete_content(ctx, p_path);
    Paths::remove_to_recycle(p_path);
}

void AssetBrowserList::_delete_folder(EditorContext& ctx, const std::filesystem::path& p_folder)
{
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(p_folder, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec)) continue;
        const std::filesystem::path ext = it->path().extension();
        if (ext == ".ltexture" || ext == ".lmesh") _delete_content(ctx, it->path());
    }
    Paths::remove_to_recycle(p_folder);
}

Guid AssetBrowserList::_resolve_texture_guid(const std::filesystem::path& p_path)
{
    if (auto it = _thumb_guids.find(p_path); it != _thumb_guids.end()) return it->second;
    AssetInfo info = read_asset_info(p_path);
    if (!info.valid() || info.type != AssetType::Texture) return Guid{};
    _thumb_guids.emplace(p_path, info.guid);
    return info.guid;
}

void AssetBrowserList::_list_item(ImTextureID , const char* , const char* )
{

}

bool AssetBrowserList::_draw_card(ImTextureID p_texture, const char* p_name, const char* p_type, const std::filesystem::path& p_path, float p_progress, bool p_importing)
{
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("##card", ImVec2(card_width, card_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + card_width, p0.y + card_height);
    const float rounding = 4.0f;
    const float pad = 6.0f;
    const float img_pad = 4.0f;

    const bool renaming = (rename_target == p_path);

    ImGui::InvisibleButton("##hit", ImVec2(card_width, card_height));

    if (!renaming && ImGui::BeginPopupContextItem()) {
        if (p_importing) {
            if (ImGui::MenuItem("Cancel Import")) cancel_request = p_path;
        } else {
            if (ImGui::MenuItem("Rename")) {
                rename_target = p_path;
                std::snprintf(rename_buf, sizeof(rename_buf), "%s", std::filesystem::is_directory(p_path) ? p_path.filename().string().c_str() : p_path.stem().string().c_str());
            }
            if (ImGui::MenuItem("Delete")) rename_delete_request = p_path;
        }
        ImGui::EndPopup();
    }

    const bool double_clicked = !renaming && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

    if (!renaming && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        const std::string path_str = p_path.string();
        ImGui::SetDragDropPayload("ASSET_PATH", path_str.c_str(), path_str.size() + 1);
        ImGui::Text("%s", p_name);
        ImGui::EndDragDropSource();
    }

    const ImVec2 thumb1(p0.x + card_width, p0.y + card_width);
    const ImVec2 foot0(p0.x, thumb1.y);
    const ImVec2 sh_off(2.0f, 2.0f);
    const ImVec2 img0(p0.x + img_pad, p0.y + img_pad);
    const ImVec2 img1(thumb1.x - img_pad, thumb1.y - img_pad);

    dl->AddRectFilled(ImVec2(p0.x + sh_off.x, p0.y + sh_off.y), ImVec2(p1.x + sh_off.x, p1.y + sh_off.y), IM_COL32(0, 0, 0, 160), rounding, ImDrawFlags_RoundCornersBottom);
    dl->AddRectFilled(p0, thumb1, ImGui::GetColorU32(ImGuiCol_FrameBg));
    if (p_texture) dl->AddImage(p_texture, img0, img1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    dl->AddRectFilled(foot0, p1, ImGui::GetColorU32(ImGuiCol_FrameBgHovered), rounding, ImDrawFlags_RoundCornersBottom);

    if (ImGui::IsItemHovered()) dl->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Text), rounding, ImDrawFlags_RoundCornersBottom, 1.0f);

    const ImVec2 name_min(foot0.x + pad, foot0.y + pad);

    if (renaming) {
        ImGui::SetCursorScreenPos(name_min);
        ImGui::SetNextItemWidth(card_width - pad * 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        const bool entered = ImGui::InputText("##rename", rename_buf, sizeof(rename_buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar();
        const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (entered || (ImGui::IsItemDeactivated() && !escaped)) { Paths::rename(p_path, rename_buf); _thumb_guids.clear(); }
        if (entered || escaped || ImGui::IsItemDeactivated()) rename_target.clear();
    } else {
        const ImVec2 name_max(p1.x - pad, name_min.y + ImGui::GetTextLineHeight());
        dl->PushClipRect(name_min, name_max, true);
        dl->AddText(name_min, ImGui::GetColorU32(ImGuiCol_Text), p_name);
        dl->PopClipRect();
    }

    if (p_type && *p_type) {
        const ImVec2 ts = ImGui::CalcTextSize(p_type);
        const ImVec2 type_pos(p1.x - pad - ts.x, p1.y - pad - ts.y);
        dl->AddText(type_pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), p_type);
    }

    if (p_progress >= 0.0f && p_progress < 1.0f) {
        const float bar_h = 3.0f;
        const ImVec2 bar0(p0.x, p1.y - bar_h);
        const ImVec2 bar1(p1.x, p1.y);
        dl->AddRectFilled(bar0, bar1, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg), rounding, ImDrawFlags_RoundCornersBottom);
        const float w = (bar1.x - bar0.x) * (p_progress < 0.0f ? 0.0f : p_progress);
        if (w > 0.5f) dl->AddRectFilled(bar0, ImVec2(bar0.x + w, bar1.y), ImGui::GetColorU32(ImGuiCol_Button), rounding, w >= (bar1.x - bar0.x) - rounding ? ImDrawFlags_RoundCornersBottom : ImDrawFlags_RoundCornersBottomLeft);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    return double_clicked;
}

void AssetBrowserList::draw(EditorContext& ctx, std::filesystem::path& selected, const char* search_buf)
{
    const float min_gap = 16.0f;
    const float row_gap = 8.0f;
    const float avail = ImGui::GetContentRegionAvail().x;

    int columns = (int)((avail + min_gap) / (card_width + min_gap));
    if (columns < 1) columns = 1;

    float gap = min_gap;
    if (columns > 1) gap = (avail - (float)columns * (float)card_width) / (float)(columns - 1);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(min_gap, row_gap));

    if (!selected.empty() && std::filesystem::exists(selected)) {
        std::vector<std::filesystem::directory_entry> entries;
        for (const auto& entry : std::filesystem::directory_iterator(selected)) entries.push_back(entry);
        std::sort(entries.begin(), entries.end(),
            [](const std::filesystem::directory_entry& a, const std::filesystem::directory_entry& b) {
                const bool a_dir = a.is_directory();
                const bool b_dir = b.is_directory();
                if (a_dir != b_dir) return a_dir;
                const std::string an = a.path().filename().string();
                const std::string bn = b.path().filename().string();
                return std::lexicographical_compare(an.begin(), an.end(), bn.begin(), bn.end(), [](unsigned char c1, unsigned char c2) { return std::tolower(c1) < std::tolower(c2); });
            });

        std::string query = search_buf;
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        auto matches = [&](const std::filesystem::path& p) {
            if (query.empty()) return true;
            std::string hay = p.filename().string();
            std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c){ return (char)std::tolower(c); });
            return hay.find(query) != std::string::npos;
        };

        int i = 0;

        for (const auto& entry : entries) {
            if (!matches(entry.path())) continue;

            const std::string name = entry.path().stem().string();
            std::string type = entry.path().extension().string();
            for (char& c : type) c = (char)toupper((unsigned char)c);

            if (i % columns != 0) ImGui::SameLine(0.0f, gap);
            ImGui::PushID(i);
            
            const float progress = ctx.imports->progress(entry.path());

            VkDescriptorSet set = VK_NULL_HANDLE;
            if (!entry.is_directory()) {
                VkImageView view = VK_NULL_HANDLE;
                if (entry.path().extension() == ".ltexture" && ctx.renderer) {
                    if (const LTexture* bt = ctx.renderer->textures.get(_resolve_texture_guid(entry.path()))) view = bt->image.image_view;
                }

                set = ctx.imgui->texture_cache.get(view);
            }

            const bool activated = _draw_card((ImTextureID)set, name.c_str(), type.c_str(), entry.path(), progress);

            if (entry.is_directory() && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) Paths::move((const char*)payload->Data, entry.path());
                ImGui::EndDragDropTarget();
            }

            if (activated && entry.is_directory()) selected = entry.path();

            if (!rename_delete_request.empty()) {
                std::filesystem::is_directory(rename_delete_request) ? _delete_folder(ctx, rename_delete_request) : _delete_asset(ctx, rename_delete_request);
                rename_delete_request.clear();
                _thumb_guids.clear();
            }
            
            ImGui::PopID();
            i++;
        }

        std::vector<std::filesystem::path> pending = ctx.imports->pending_out(selected);
        for (const auto& ppath : pending) {
            if (!matches(ppath)) continue;
            if (i % columns != 0) ImGui::SameLine(0.0f, gap);
            ImGui::PushID(i);
            _draw_card(0, ppath.stem().string().c_str(), "IMPORTING", ppath, ctx.imports->progress(ppath), true);
            ImGui::PopID();
            ++i;
        }

        if (!cancel_request.empty()) {
            // ctx.imports->cancel(cancel_request);
            cancel_request.clear();
        }
    }
    
    ImGui::PopStyleVar();
}

}