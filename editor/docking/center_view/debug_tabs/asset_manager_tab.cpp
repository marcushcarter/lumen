#include <editor/docking/center_view/debug_tabs/asset_manager_tab.h>
#include <editor/assets/asset_import_any.h>
#include <editor/assets/asset_import_tracker.h>
#include <editor/editor_resources.h>
#include <core/rendering/renderer.h>
#include <core/project/project.h>
#include <core/io/path.h>
#include <core/base/error.h>
#include <drivers/imgui/imgui_driver.h>
#include <drivers/imgui/imgui_helpers.h>
#include <drivers/windows/dialogs_win32.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>

namespace lumen {

/***************/
/**** CACHE ****/
/***************/

void AssetManagerDebugTab::_cache_tick(double p_now)
{
    if (!refresh_requested && p_now - last_refresh < CACHE_REFRESH_INTERVAL_S) return;
    dirs.clear();
    last_refresh = p_now;
    refresh_requested = false;
}

AssetManagerDebugTab::Dir& AssetManagerDebugTab::_cache_get(const std::filesystem::path& p_dir)
{
    auto [it, inserted] = dirs.try_emplace(p_dir);
    if (inserted) _cache_scan(p_dir, it->second);
    return it->second;
}

void AssetManagerDebugTab::_cache_scan(const std::filesystem::path& p_dir, Dir& r_dir)
{
    r_dir.path_str = p_dir.string();
    r_dir.name = p_dir.filename().string();

    std::error_code ec;
    r_dir.exists = std::filesystem::is_directory(p_dir, ec);
    if (!r_dir.exists) return;

    for (auto it = std::filesystem::directory_iterator(p_dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        Entry e;
        e.path = it->path();
        e.is_dir = it->is_directory(ec);
        e.name = e.path.stem().string();
        e.type = e.path.extension().string();
        for (char& c : e.type) c = (char)std::toupper((unsigned char)c);
        e.lower_name = e.path.filename().string();
        for (char& c : e.lower_name) c = (char)std::tolower((unsigned char)c);
        e.is_texture = !e.is_dir && e.path.extension() == ".ltexture";
        if (e.is_dir) r_dir.subdirs.push_back(e.path);
        r_dir.entries.push_back(std::move(e));
    }

    std::sort(r_dir.subdirs.begin(), r_dir.subdirs.end());
    std::sort(r_dir.entries.begin(), r_dir.entries.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        return a.lower_name < b.lower_name;
    });
}

/**************/
/**** TREE ****/
/**************/

void AssetManagerDebugTab::_draw_folder_node(const std::filesystem::path& dir, int depth)
{
    Dir& node = _cache_get(dir);
    ImGui::PushID(node.path_str.c_str());

    ImGuiStorage* storage  = ImGui::GetStateStorage();
    const ImGuiID open_key = ImGui::GetID("open");
    bool open = storage->GetBool(open_key, depth == 0);

    const bool has_children = !node.subdirs.empty();
    if (!has_children) open = false;

    float indent_w = 14.0f;
    const float row_h = ImGui::GetTextLineHeight() + 4.0f;
    const float full_w = ImGui::GetContentRegionAvail().x;
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const ImVec2 row_max = ImVec2(row_min.x + full_w, row_min.y + row_h);
    const float indent = depth * indent_w;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool selected_here = (selected_folder == dir);
    const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(row_min, row_max);
    if (selected_here) dl->AddRectFilled(row_min, row_max, ImGui::GetColorU32(ImGuiCol_Header));
    else if (hovered) dl->AddRectFilled(row_min, row_max, ImGui::GetColorU32(ImGuiCol_HeaderHovered));

    float chevron_w = 18.0f;
    const ImVec2 chev_min(row_min.x + indent, row_min.y);
    if (has_children) {
        ImGui::SetCursorScreenPos(chev_min);
        if (ImGui::InvisibleButton("chevron", ImVec2(chevron_w, row_h))) {
            open = !open; storage->SetBool(open_key, open);
        }
        const bool chev_hover = ImGui::IsItemHovered();
        const ImU32 tri_col = ImGui::GetColorU32(chev_hover ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        const float fs = ImGui::GetFontSize();
        const ImVec2 arrow_pos(chev_min.x + (chevron_w - fs) * 0.5f, row_min.y + (row_h - fs) * 0.5f);
        ImGui::RenderArrow(dl, arrow_pos, tri_col, open ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);
    }

    const float body_x = row_min.x + indent + chevron_w;
    float body_w = row_max.x - body_x;
    if (body_w < 1.0f) body_w = 1.0f;
    ImGui::SetCursorScreenPos(ImVec2(body_x, row_min.y));
    if (ImGui::InvisibleButton("row", ImVec2(body_w, row_h))) selected_folder = dir;
    if (has_children && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        open = !open; storage->SetBool(open_key, open);
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
            const AssetDragPayload* asset = (const AssetDragPayload*)payload->Data;
            if (asset->path[0]) Paths::move(asset->path, dir);
            request_refresh();
        }
        ImGui::EndDragDropTarget();
    }

    const char* icon = open ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;
    const ImVec2 is = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(body_x, row_min.y + (row_h - is.y) * 0.5f), IM_COL32(224, 187, 88, 255), icon);

    float icon_gap = 6.0f;
    const float name_x = body_x + is.x + icon_gap;
    dl->PushClipRect(ImVec2(name_x, row_min.y), row_max, true);
    dl->AddText(ImVec2(name_x, row_min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), node.name.c_str());
    dl->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(row_min.x, row_max.y));
    if (open) for (const auto& sub : node.subdirs) _draw_folder_node(sub, depth + 1);

    ImGui::PopID();
}

/*****************/
/**** TOOLBAR ****/
/*****************/

void AssetManagerDebugTab::_toolbar_breadcrumb(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> chain;
    chain.push_back(root); 
    if (!selected_folder.empty()) {
        std::filesystem::path accum = root;
        for (const auto& part : selected_folder.lexically_relative(root)) {
            if (part == ".") continue;
            accum /= part;
            chain.push_back(accum);
        }
    }

    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    for (size_t k = 0; k < chain.size(); ++k) {
        if (k > 0) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled("/");
            ImGui::SameLine(0.0f, 4.0f);
        }
        
        const bool is_last = (k + 1 == chain.size());
        const std::string name = chain[k].filename().string();

        ImGui::PushID((int)k);
        ImGui::PushStyleColor(ImGuiCol_Text, is_last ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
        if (ImGui::Button(name.c_str())) selected_folder = chain[k];
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::PopStyleColor();
}

void AssetManagerDebugTab::_toolbar_draw(EditorContext& ctx, const std::filesystem::path& root)
{
    ImGui::BeginDisabled(!(!selected_folder.empty() && selected_folder != root));
    if (ImGui::Button(ICON_FA_CHEVRON_LEFT)) selected_folder = selected_folder.parent_path();
    ImGui::EndDisabled();
    
    ImGui::SameLine();
    if (ImGui::Button("New Folder")) {
        std::filesystem::create_directory(ctx.project->assets_dir / "New Folder");
        request_refresh();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("+ Import")) {
        if (!selected_folder.empty() && std::filesystem::exists(selected_folder)) {
            std::vector<std::wstring> files = drivers::Win32Dialogs::open_files(
                L"All Supported\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0"
                L"Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0"
                L"All Files\0*.*\0"
            );
            for (const std::wstring& f : files) {
                const std::filesystem::path source = f;
                if (!asset_import_any(ctx, source, selected_folder)) log_write("Skipped unsupported import: %s", source.string().c_str());
            }
            request_refresh();
        }
    }
    
    ImGui::SameLine();
    ImGui::InputTextWithHint("##search", "Search..", search_buf, sizeof(search_buf));
    
    ImGui::SameLine();
    _toolbar_breadcrumb(root);
}

/**************/
/**** LIST ****/
/**************/

void AssetManagerDebugTab::_delete_content(EditorContext& ctx, const std::filesystem::path& p_asset)
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

void AssetManagerDebugTab::_delete_asset(EditorContext& ctx, const std::filesystem::path& p_path)
{
    _delete_content(ctx, p_path);
    Paths::remove_to_recycle(p_path);
}

void AssetManagerDebugTab::_delete_folder(EditorContext& ctx, const std::filesystem::path& p_folder)
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

Guid AssetManagerDebugTab::_resolve_texture_guid(const std::filesystem::path& p_path)
{
    if (auto it = _thumb_guids.find(p_path); it != _thumb_guids.end()) return it->second;
    AssetInfo info = read_asset_info(p_path);
    if (!info.valid() || info.type != AssetType::TEXTURE) return Guid{};
    _thumb_guids.emplace(p_path, info.guid);
    return info.guid;
}

bool AssetManagerDebugTab::_list_item(ImTextureID p_texture, const char* p_name, const char* p_type, const std::filesystem::path& p_path, float p_progress, bool p_importing)
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
        if (!ImGui::GetDragDropPayload()) drag_payload = AssetDragPayload::make(p_path);
        ImGui::SetDragDropPayload(AssetDragPayload::TYPE, &drag_payload, sizeof(drag_payload), ImGuiCond_Once);
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
            request_refresh();
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

void AssetManagerDebugTab::_list_draw(EditorContext& ctx)
{
    const float min_gap = 16.0f;
    const float row_gap = 8.0f;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(min_gap, row_gap));

    Dir* dir = selected_folder.empty() ? nullptr : &_cache_get(selected_folder);
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
                Entry& e = dir->entries[_visible[row]];
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
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
                        const AssetDragPayload* asset = (const AssetDragPayload*)payload->Data;
                        if (asset->path[0]) Paths::move(asset->path, e.path);
                        request_refresh();
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
            std::vector<std::filesystem::path> pending = ctx.imports->pending_out(selected_folder);
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
            request_refresh();
        }

        if (!cancel_request.empty()) {
            // ctx.imports->cancel(cancel_request);
            cancel_request.clear();
        }

        if (!open_request.empty()) selected_folder = open_request;
    }
    
    ImGui::PopStyleVar();
}

/*******************/
/**** DEBUG TAB ****/
/*******************/

void AssetManagerDebugTab::draw(EditorContext& ctx)
{
    registry = ctx.assets;
    if (selected_folder.empty()) selected_folder = ctx.project->assets_dir;
    const std::filesystem::path& root = ctx.project->assets_dir;
    _cache_tick(ImGui::GetTime());
    
    const ImVec2 region_p0 = ImGui::GetCursorScreenPos();

    const float thick = 6.0f;
    ImVec2 avail = ImGui::GetContentRegionAvail();

    float body_w = avail.x - thick;
    if (body_w < 1.0f) body_w = 1.0f;
    const float min_side = 120.0f;
    split_x = ImClamp(split_x, min_side / body_w, 1.0f - min_side / body_w);
    float left_w = ImFloor(body_w * split_x);
    float right_w = body_w - left_w;

    ImGui::BeginChild("##left", ImVec2(left_w, avail.y), true);
    if (_cache_get(root).exists) _draw_folder_node(root, 0);
    {
        // Drawn in the tree child's own list: the foreground list painted this over popups and the drag tooltip.
        const float divider_x = region_p0.x + left_w;
        const float shadow_w  = 20.0f;
        const ImU32 c_edge = IM_COL32(0, 0, 0, 80);
        const ImU32 c_fade = IM_COL32(0, 0, 0, 0);
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(ImVec2(divider_x - shadow_w, region_p0.y), ImVec2(divider_x, region_p0.y + avail.y), c_fade, c_edge, c_edge, c_fade);
    }
    ImGui::EndChild();

    ImGui::SameLine(0, 0);
    SplitterState sx = imgui_splitter("##am_split_lr", SplitAxis::X, ImVec2(thick, avail.y));
    if (sx.active) split_x += sx.delta / body_w;
    ImGui::SameLine(0, 0);

    ImGui::BeginChild("##right", ImVec2(right_w, avail.y), true);
    _toolbar_draw(ctx, root);
    ImGui::BeginChild("##bottom_right", ImVec2(0, 0), true);
    _list_draw(ctx);
    ImGui::EndChild();
    ImGui::EndChild();
    
    // const float divider_x = region_p0.x + left_w;
    // const float shadow_w  = 20.0f;
    // const ImU32 c_edge = IM_COL32(0, 0, 0, 80);
    // const ImU32 c_fade = IM_COL32(0, 0, 0, 0);
    // ImDrawList* dl = ImGui::GetForegroundDrawList();
    // dl->AddRectFilledMultiColor(ImVec2(divider_x - shadow_w, region_p0.y), ImVec2(divider_x, region_p0.y + avail.y), c_fade, c_edge, c_edge, c_fade);

}

}
