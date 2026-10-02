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
            case AssetType::TEXTURE: ctx.renderer->textures.unload(info.guid); break;
            case AssetType::MESH: ctx.renderer->geometry.unload(info.guid); break;
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
    if (!info.valid() || info.type != AssetType::TEXTURE) return Guid{};
    _thumb_guids.emplace(p_path, info.guid);
    return info.guid;
}

bool AssetBrowserList::_list_item(ImTextureID p_texture, const char* p_name, const char* p_type, const std::filesystem::path& p_path, float p_progress, bool p_importing)
{
    const float list_item_height = 48.0f;
    const float icon_size = 36.0f;
    const float pad = 6.0f;
    const float rounding = 4.0f;
    const float width = ImGui::GetContentRegionAvail().x;

    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("##list_item", ImVec2(width, list_item_height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + width, p0.y + list_item_height);

    const bool renaming = (rename_target == p_path);

    ImGui::InvisibleButton("##hit", ImVec2(width, list_item_height));
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

    const ImVec2 icon0(p0.x + pad, p0.y + (list_item_height - icon_size) * 0.5f);
    const ImVec2 icon1(icon0.x + icon_size, icon0.y + icon_size);
    const ImVec2 text0(icon1.x + pad, p0.y + 7.0f);

    dl->AddRectFilled(p0, p1, ImGui::IsItemHovered() ? ImGui::GetColorU32(ImGuiCol_FrameBgHovered) : ImGui::GetColorU32(ImGuiCol_FrameBg), rounding);
    if (p_texture) dl->AddImage(p_texture, icon0, icon1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE);
    else dl->AddRectFilled(icon0, icon1, ImGui::GetColorU32(ImGuiCol_FrameBgActive), rounding);
    if (ImGui::IsItemHovered()) dl->AddRect(p0, p1, ImGui::GetColorU32(ImGuiCol_Text), rounding, 0, 1.0f);

    if (renaming) {
        const float text_width = width - (text0.x - p0.x) - pad;
        ImGui::SetCursorScreenPos(text0);
        ImGui::SetNextItemWidth(text_width);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        const bool entered = ImGui::InputText("##rename", rename_buf, sizeof(rename_buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar();

        const bool escaped = ImGui::IsKeyPressed(ImGuiKey_Escape);
        if (entered || (ImGui::IsItemDeactivated() && !escaped)) {
            Paths::rename(p_path, rename_buf);
            _thumb_guids.clear();
            if (_cache) _cache->request_refresh();
        }
        if (entered || escaped || ImGui::IsItemDeactivated()) rename_target.clear();
    } else {
        const ImVec2 text_max(p1.x - pad, p1.y - pad);
        dl->PushClipRect(text0, text_max, true);
        dl->AddText(text0, ImGui::GetColorU32(ImGuiCol_Text), p_name);
        if (p_type && *p_type) {
            const ImVec2 subtext_pos(text0.x, text0.y + ImGui::GetTextLineHeight() + 1.0f);
            dl->AddText(subtext_pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), p_type);
        }
        dl->PopClipRect();
    }

    if (p_progress >= 0.0f && p_progress < 1.0f) {
        const float bar_h = 2.0f;
        const ImVec2 bar0(p0.x, p1.y - bar_h);
        const ImVec2 bar1(p1.x, p1.y);
        dl->AddRectFilled(bar0, bar1, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
        const float w = (bar1.x - bar0.x) * p_progress;
        if (w > 0.5f) {
            dl->AddRectFilled(bar0, ImVec2(bar0.x + w, bar1.y), ImGui::GetColorU32(ImGuiCol_Button));
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    return double_clicked;
}

void AssetBrowserList::draw(EditorContext& ctx, AssetDirCache& cache, std::filesystem::path& selected, const char* search_buf)
{
    _cache = &cache;

    const float min_gap = 16.0f;
    const float row_gap = 8.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(min_gap, row_gap));

    AssetDirCache::Dir* dir = selected.empty() ? nullptr : &cache.get(selected);
    if (dir && dir->exists) {
        std::string query = search_buf;
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        _visible.clear();
        for (uint32_t k = 0; k < (uint32_t)dir->entries.size(); ++k) {
            if (query.empty() || dir->entries[k].lower_name.find(query) != std::string::npos) _visible.push_back(k);
        }

        const bool importing = !ctx.imports->pending.empty();
        std::filesystem::path open_request;

        ImGuiListClipper clipper;
        clipper.Begin((int)_visible.size());
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                AssetDirCache::Entry& e = dir->entries[_visible[row]];
                ImGui::PushID(row);

                const float progress = importing ? ctx.imports->progress(e.path) : -1.0f;

                VkDescriptorSet set = VK_NULL_HANDLE;
                if (!e.is_dir) {
                    VkImageView view = VK_NULL_HANDLE;
                    if (e.is_texture && ctx.renderer) {
                        if (!e.guid_checked) {
                            e.guid = _resolve_texture_guid(e.path);
                            e.guid_checked = true;
                        }
                        if (const LTexture* bt = ctx.renderer->textures.get(e.guid)) view = bt->image.image_view;
                    }
                    set = ctx.imgui->texture_cache.get(view);
                }

                const bool activated = _list_item((ImTextureID)set, e.name.c_str(), e.type.c_str(), e.path, progress);

                if (e.is_dir && ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                        Paths::move((const char*)payload->Data, e.path);
                        cache.request_refresh();
                    }
                    ImGui::EndDragDropTarget();
                }

                if (activated && e.is_dir) open_request = e.path;
                ImGui::PopID();
            }
        }
        clipper.End();

        if (importing) {
            int i = (int)_visible.size();
            std::vector<std::filesystem::path> pending = ctx.imports->pending_out(selected);
            for (const auto& ppath : pending) {
                std::string lower = ppath.filename().string();
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (!query.empty() && lower.find(query) == std::string::npos) continue;

                ImGui::PushID(i);
                _list_item(0, ppath.stem().string().c_str(), "IMPORTING", ppath, ctx.imports->progress(ppath), true);
                ImGui::PopID();
                ++i;
            }
        }

        if (!rename_delete_request.empty()) {
            std::filesystem::is_directory(rename_delete_request) ? _delete_folder(ctx, rename_delete_request) : _delete_asset(ctx, rename_delete_request);
            rename_delete_request.clear();
            _thumb_guids.clear();
            cache.request_refresh();
        }

        if (!cancel_request.empty()) {
            // ctx.imports->cancel(cancel_request);
            cancel_request.clear();
        }

        if (!open_request.empty()) selected = open_request;
    }
    
    ImGui::PopStyleVar();
}

}