#include <editor/panels/content_browser_panel.h>
#include <core/assets/lmodel.h>
#include <editor/assets/write_atomic.h>
#include <toml++/toml.hpp>
#include <sstream>
#include <drivers/imgui/ui_style.h>
#include <editor/assets/import/asset_import_any.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/editor_resources.h>
#include <core/rendering/renderer.h>
#include <core/project/project.h>
#include <core/io/path.h>
#include <core/base/error.h>
#include <drivers/imgui/imgui_driver.h>
#include <drivers/imgui/imgui_helpers.h>
#include <drivers/imgui/tab_strip.h>
#include <editor/tabs/tab_manager.h>
#include <editor/tabs/model/model_tab.h>
#include <editor/tabs/material/material_tab.h>
#include <editor/assets/texture_viewer.h>
#include <editor/assets/thumbnail_cache.h>
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

static std::string _type_name(std::string p_ext)
{
    for (char& c : p_ext) c = (char)std::tolower((unsigned char)c);
    if (p_ext == ".lmesh") return "Mesh";
    if (p_ext == ".ltexture") return "Texture";
    if (p_ext == ".lmodel") return "Model";
    if (p_ext == ".lmaterial") return "Material";
    if (!p_ext.empty() && p_ext[0] == '.') p_ext.erase(0, 1);
    for (char& c : p_ext) c = (char)std::toupper((unsigned char)c);
    return p_ext;
}

void ContentBrowserPanel::_cache_tick(double p_now)
{
    if (!refresh_requested && p_now - last_refresh < CACHE_REFRESH_INTERVAL_S) return;
    dirs.clear();
    last_refresh = p_now;
    refresh_requested = false;
}

ContentBrowserPanel::Dir& ContentBrowserPanel::_cache_get(const std::filesystem::path& p_dir)
{
    auto [it, inserted] = dirs.try_emplace(p_dir);
    if (inserted) _cache_scan(p_dir, it->second);
    return it->second;
}

void ContentBrowserPanel::_cache_scan(const std::filesystem::path& p_dir, Dir& r_dir)
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
        e.type = _type_name(e.path.extension().string());
        e.lower_name = e.path.filename().string();
        for (char& c : e.lower_name) c = (char)std::tolower((unsigned char)c);
        e.is_texture = !e.is_dir && e.path.extension() == ".ltexture";
        if (e.is_dir) e.type = "Folder";
        e.icon = e.is_dir ? ICON_FA_FOLDER : e.is_texture ? ICON_FA_IMAGE : e.path.extension() == ".lmesh" ? ICON_FA_CUBE : e.path.extension() == ".lmodel" ? ModelTab::ICON : e.path.extension() == ".lmaterial" ? MaterialTab::ICON : ICON_FA_FILE;
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

void ContentBrowserPanel::_draw_folder_node(const std::filesystem::path& dir, int depth)
{
    Dir& node = _cache_get(dir);
    ImGui::PushID(node.path_str.c_str());

    ImGuiStorage* storage  = ImGui::GetStateStorage();
    const ImGuiID open_key = ImGui::GetID("open");
    bool open = storage->GetBool(open_key, depth == 0);

    const bool has_children = !node.subdirs.empty();
    if (!has_children) open = false;

    const UiStyle& ui = ui_style();
    const float indent_w = ImGui::GetFontSize();
    const float row_h = ImGui::GetTextLineHeight() + ui.asset_browser.row_pad_y;
    const float full_w = ImGui::GetContentRegionAvail().x;
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const ImVec2 row_max = ImVec2(row_min.x + full_w, row_min.y + row_h);
    const float indent = depth * indent_w;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool selected_here = (selected_folder == dir);
    const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(row_min, row_max);
    if (selected_here) dl->AddRectFilled(row_min, row_max, ImGui::GetColorU32(ImGuiCol_Header));
    else if (hovered) dl->AddRectFilled(row_min, row_max, ImGui::GetColorU32(ImGuiCol_HeaderHovered));

    const float chevron_w = ui.asset_browser.chevron_width;
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
    _drop_target(dir);

    const char* icon = open ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;
    const ImVec2 is = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(body_x, row_min.y + (row_h - is.y) * 0.5f), ui.color.folder, icon);

    const float icon_gap = ui.asset_browser.icon_gap;
    const float name_x = body_x + is.x + icon_gap;
    dl->PushClipRect(ImVec2(name_x, row_min.y), row_max, true);
    dl->AddText(ImVec2(name_x, row_min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), node.name.c_str());
    dl->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(row_min.x, row_max.y));
    if (open) for (const auto& sub : node.subdirs) _draw_folder_node(sub, depth + 1);

    ImGui::PopID();
}

/**************/
/**** LIST ****/
/**************/

void ContentBrowserPanel::_delete_content(EditorContext& ctx, const std::filesystem::path& p_asset)
{
    AssetInfo info = read_asset_info(p_asset);
    if (!info.valid()) return;

    if (ctx.renderer) {
        switch (info.type) {
            case AssetType::TEXTURE: ctx.renderer->textures.unload(info.guid); break;
            case AssetType::MESH: ctx.renderer->geometry.unload(info.guid); break;
            case AssetType::MODEL: ctx.renderer->models.unload(info.guid); break;
            case AssetType::MATERIAL: ctx.renderer->materials.unload(info.guid); break;
            default: break;
        }
    }

    Paths::remove_to_recycle(ctx.project->content_path(info.guid));
}

void ContentBrowserPanel::_delete_asset(EditorContext& ctx, const std::filesystem::path& p_path)
{
    _delete_content(ctx, p_path);
    Paths::remove_to_recycle(p_path);
}

void ContentBrowserPanel::_delete_folder(EditorContext& ctx, const std::filesystem::path& p_folder)
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

AssetInfo ContentBrowserPanel::_resolve_asset(const std::filesystem::path& p_path)
{
    if (auto it = _thumb_infos.find(p_path); it != _thumb_infos.end()) return it->second;
    AssetInfo info = read_asset_info(p_path);
    if (!info.valid()) info = AssetInfo{};
    _thumb_infos.emplace(p_path, info);
    return info;
}

float ContentBrowserPanel::_row_height() const
{
    const UiStyle& ui = ui_style();
    return ImFloor(ImGui::GetTextLineHeight() * 2.0f + ui.asset_browser.item_subtitle_gap + ui.asset_browser.row_pad_y * 2.0f);
}

static std::filesystem::path _unique_path(const std::filesystem::path& p_parent, const char* p_stem, const char* p_ext)
{
    std::error_code ec;
    std::filesystem::path path = p_parent / (std::string(p_stem) + p_ext);
    for (int i = 1; std::filesystem::exists(path, ec); ++i) path = p_parent / (std::string(p_stem) + " " + std::to_string(i) + p_ext);
    return path;
}

static bool _duplicable(const std::filesystem::path& p_path)
{
    const std::filesystem::path ext = p_path.extension();
    return ext == ".lmodel" || ext == ".lmaterial";
}

static std::filesystem::path _duplicate_asset(const std::filesystem::path& p_path)
{
    toml::table tbl;
    try { tbl = toml::parse_file(p_path.string()); }
    catch (const toml::parse_error&) { return {}; }
    toml::table* asset = tbl["asset"].as_table();
    if (!asset) return {};
    asset->insert_or_assign("guid", Guid::generate().to_string());

    std::ostringstream ss;
    ss << tbl;
    const std::string text = ss.str();
    const std::filesystem::path dst = _unique_path(p_path.parent_path(), p_path.stem().string().c_str(), p_path.extension().string().c_str());
    if (!write_file_atomic(dst, text.data(), text.size())) return {};
    return dst;
}

void ContentBrowserPanel::_duplicate_selection(EditorContext& ctx)
{
    std::vector<std::filesystem::path> created;
    for (const std::filesystem::path& p : selection) {
        if (!_duplicable(p)) continue;
        const std::filesystem::path dst = _duplicate_asset(p);
        if (dst.empty()) {
            log_write("Content Browser: failed to duplicate '%s'.", p.string().c_str());
            continue;
        }
        if (ctx.renderer && dst.extension() == ".lmodel") ctx.renderer->models.load(dst);
        if (ctx.renderer && dst.extension() == ".lmaterial") ctx.renderer->materials.load(dst);
        created.push_back(dst);
    }
    if (created.empty()) return;
    selection = created;
    anchor = created.back();
    if (created.size() == 1) {
        _rename_begin(created.front());
        _scroll_to_rename = true;
    }
    request_refresh();
}

void ContentBrowserPanel::_new_folder(EditorContext& ctx)
{
    const std::filesystem::path parent = selected_folder.empty() ? ctx.project->assets_dir : selected_folder;
    std::error_code ec;
    const std::filesystem::path path = _unique_path(parent, "New Folder", "");
    if (!std::filesystem::create_directory(path, ec)) return;
    if (!selection.empty()) _move_selection(path);
    _select_only(path);
    _rename_begin(path);
    _scroll_to_rename = true;
    request_refresh();
}

void ContentBrowserPanel::_import(EditorContext& ctx)
{
    std::error_code ec;
    if (selected_folder.empty() || !std::filesystem::exists(selected_folder, ec)) return;
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

void ContentBrowserPanel::_move_selection(const std::filesystem::path& p_dst)
{
    for (const std::filesystem::path& p : selection) {
        bool nested = false;
        for (const std::filesystem::path& o : selection) nested = nested || (o != p && Paths::is_under(p, o));
        if (nested || p == p_dst || Paths::is_under(p_dst, p)) continue;
        Paths::move(p, p_dst);
    }
    _clear_selection();
    _thumb_infos.clear();
    request_refresh();
}

void ContentBrowserPanel::_delete_folders_only(EditorContext& ctx)
{
    std::error_code ec;
    for (const std::filesystem::path& p : selection) {
        if (!std::filesystem::is_directory(p, ec)) continue;
        std::vector<std::filesystem::path> children;
        for (auto it = std::filesystem::directory_iterator(p, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) children.push_back(it->path());
        for (const std::filesystem::path& child : children) Paths::move(child, p.parent_path());
        if (std::filesystem::is_empty(p, ec)) Paths::remove_to_recycle(p);
        else log_write("Content Browser: '%s' kept, some items already exist in the parent folder.", p.string().c_str());
    }
    (void)ctx;
    _clear_selection();
    _thumb_infos.clear();
    request_refresh();
}

void ContentBrowserPanel::_drop_target(const std::filesystem::path& p_dst)
{
    if (!ImGui::BeginDragDropTarget()) return;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
        const AssetDragPayload* asset = (const AssetDragPayload*)payload->Data;
        if (asset->path[0]) {
            if (_is_selected(asset->path)) _move_selection(p_dst);
            else Paths::move(asset->path, p_dst);
        }
        request_refresh();
    }
    ImGui::EndDragDropTarget();
}

void ContentBrowserPanel::_context_menu(EditorContext& ctx, const std::filesystem::path* p_row)
{
    if (!p_row) return;

    std::error_code ec;
    const uint32_t count = (uint32_t)selection.size();
    bool any_folder = false;
    for (const std::filesystem::path& p : selection) any_folder = any_folder || std::filesystem::is_directory(p, ec);

    if (ImGui::MenuItem(ICON_FA_PEN "  Rename", "F2")) _rename_begin(*p_row);
    if (_duplicable(*p_row) && ImGui::MenuItem(ICON_FA_COPY "  Duplicate")) _duplicate_selection(ctx);
    if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS "  Move To New Folder")) _new_folder(ctx);
    
    ImGui::Separator();
    char del[48];
    snprintf(del, sizeof(del), count > 1 ? ICON_FA_TRASH_CAN "  Delete %u Items" : ICON_FA_TRASH_CAN "  Delete", count);
    if (ImGui::MenuItem(del, "Del")) _delete_selection();
    if (any_folder) {
        if (ImGui::MenuItem(ICON_FA_FOLDER_MINUS "  Delete Folder Only")) _delete_folders_only(ctx);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Removes the folder and moves its contents to the parent folder");
    }
}

void ContentBrowserPanel::_rename_begin(const std::filesystem::path& p_path)
{
    rename_target = p_path;
    std::error_code ec;
    std::snprintf(rename_buf, sizeof(rename_buf), "%s", std::filesystem::is_directory(p_path, ec) ? p_path.filename().string().c_str() : p_path.stem().string().c_str());
    rename_focus = true;
}

void ContentBrowserPanel::_rename_commit()
{
    if (rename_buf[0] == '\0' || rename_target.empty()) return;
    std::filesystem::path dst = rename_target;
    dst.replace_filename(std::string(rename_buf) + rename_target.extension().string());
    Paths::rename(rename_target, rename_buf);
    for (std::filesystem::path& p : selection) if (p == rename_target) p = dst;
    if (anchor == rename_target) anchor = dst;
    _thumb_infos.clear();
    request_refresh();
}

bool ContentBrowserPanel::_is_selected(const std::filesystem::path& p_path) const
{
    for (const std::filesystem::path& p : selection) if (p == p_path) return true;
    return false;
}

void ContentBrowserPanel::_select_only(const std::filesystem::path& p_path)
{
    selection.clear();
    selection.push_back(p_path);
    anchor = p_path;
}

void ContentBrowserPanel::_clear_selection()
{
    selection.clear();
    anchor.clear();
}

void ContentBrowserPanel::_click(const Dir& p_dir, uint32_t p_visible, bool p_ctrl, bool p_shift)
{
    const std::filesystem::path& path = p_dir.entries[_visible[p_visible]].path;
    int from = -1;
    if (p_shift && !anchor.empty()) {
        for (uint32_t k = 0; k < (uint32_t)_visible.size(); ++k) {
            if (p_dir.entries[_visible[k]].path == anchor) { from = (int)k; break; }
        }
    }
    if (from >= 0) {
        if (!p_ctrl) selection.clear();
        const int lo = ImMin(from, (int)p_visible);
        const int hi = ImMax(from, (int)p_visible);
        for (int k = lo; k <= hi; ++k) {
            const std::filesystem::path& p = p_dir.entries[_visible[k]].path;
            if (!_is_selected(p)) selection.push_back(p);
        }
        for (size_t k = 0; k < selection.size(); ++k) {
            if (selection[k] != path) continue;
            selection.erase(selection.begin() + k);
            break;
        }
        selection.push_back(path);
    } else if (p_ctrl) {
        bool removed = false;
        for (size_t k = 0; k < selection.size(); ++k) {
            if (selection[k] != path) continue;
            selection.erase(selection.begin() + k);
            removed = true;
            break;
        }
        if (!removed) selection.push_back(path);
        anchor = path;
    } else {
        _select_only(path);
    }
}

void ContentBrowserPanel::_delete_selection()
{
    uint32_t folders = 0;
    uint32_t contents = 0;
    const std::filesystem::path* first = nullptr;
    std::error_code ec;
    for (const std::filesystem::path& p : selection) {
        if (!std::filesystem::is_directory(p, ec)) continue;
        bool nested = false;
        for (const std::filesystem::path& o : selection) nested = nested || (o != p && Paths::is_under(p, o));
        if (nested) continue;
        uint32_t count = 0;
        for (auto it = std::filesystem::recursive_directory_iterator(p, ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) count++;
        if (count == 0) continue;
        contents += count;
        if (!first) first = &p;
        folders++;
    }
    if (folders == 0) {
        delete_requested = true;
        return;
    }
    if (folders == 1) snprintf(delete_message, sizeof(delete_message), "Delete '%s' with %u items?", first->filename().string().c_str(), contents);
    else snprintf(delete_message, sizeof(delete_message), "Delete %u folders with %u items?", folders, contents);
    delete_confirm_open = true;
}

void ContentBrowserPanel::_delete_now(EditorContext& ctx)
{
    std::error_code ec;
    for (const std::filesystem::path& p : selection) {
        bool nested = false;
        for (const std::filesystem::path& o : selection) nested = nested || (o != p && Paths::is_under(p, o));
        if (nested || !std::filesystem::exists(p, ec)) continue;
        if (std::filesystem::is_directory(p, ec)) _delete_folder(ctx, p);
        else _delete_asset(ctx, p);
    }
    _clear_selection();
    _thumb_infos.clear();
    request_refresh();
}

void ContentBrowserPanel::_draw_delete_confirm(EditorContext& ctx)
{
    static constexpr const char* POPUP = "Delete Folder###content_browser_delete";
    if (delete_confirm_open) {
        ImGui::OpenPopup(POPUP);
        delete_confirm_open = false;
    }
    const UiStyle& ui = ui_style();
    dock_menu_push_style();
    if (ImGui::BeginPopupModal(POPUP, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted(delete_message);
        ImGui::TextDisabled("Deleted files go to the Recycle Bin.");
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ui.color.danger_soft);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.danger_soft_active);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.danger_strong);
        if (ImGui::Button("Delete", ImVec2(ui.project_manager.button_width, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            _delete_now(ctx);
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(ui.project_manager.button_width, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    dock_menu_pop_style();
}

ContentBrowserPanel::ItemResult ContentBrowserPanel::_list_item(ImTextureID p_texture, const char* p_icon, ImU32 p_icon_col, const char* p_name, const char* p_type, const std::filesystem::path& p_path, float p_progress, bool p_importing)
{
    const UiStyle& ui = ui_style();
    const float row_h = _row_height();
    const float pad_x = ui.asset_browser.item_pad;
    const float pad_y = ui.asset_browser.row_pad_y;
    const float icon_size = row_h - pad_y * 2.0f;
    const float line_h = ImGui::GetTextLineHeight();

    const bool renaming = rename_target == p_path;
    const bool selected = _is_selected(p_path);
    ItemResult result;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + ImGui::GetContentRegionAvail().x, p0.y + row_h);

    result.clicked = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, row_h));
    result.context = ImGui::IsItemClicked(ImGuiMouseButton_Right);
    result.activated = !renaming && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (result.context && !selected && !p_importing) _select_only(p_path);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ui.outliner.row_pad_y * 2.0f));
    dock_menu_push_style();
    if (!renaming && ImGui::BeginPopupContextItem("##row_menu")) {
        if (p_importing) {
            if (ImGui::MenuItem("Cancel Import")) cancel_request = p_path;
        } else if (_ctx) {
            _context_menu(*_ctx, &p_path);
        }
        ImGui::EndPopup();
    }
    dock_menu_pop_style();
    ImGui::PopStyleVar();

    if (!renaming && !p_importing && ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        if (!_is_selected(p_path)) _select_only(p_path);
        if (!ImGui::GetDragDropPayload()) drag_payload = AssetDragPayload::make(p_path);
        ImGui::SetDragDropPayload(AssetDragPayload::TYPE, &drag_payload, sizeof(drag_payload), ImGuiCond_Once);
        if (selection.size() > 1) ImGui::Text(ICON_FA_LAYER_GROUP "  %u items", (uint32_t)selection.size());
        else ImGui::Text("%s  %s", p_icon ? p_icon : "", p_name);
        ImGui::EndDragDropSource();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 icon0(p0.x + pad_x, p0.y + pad_y);
    const ImVec2 icon1(icon0.x + icon_size, icon0.y + icon_size);
    if (p_texture) {
        dl->AddImageRounded(p_texture, icon0, icon1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, ui.radius.sm);
    } else if (p_icon) {
        const ImVec2 is = ImGui::CalcTextSize(p_icon);
        dl->AddText(ImVec2(ImFloor(icon0.x + (icon_size - is.x) * 0.5f), ImFloor(icon0.y + (icon_size - is.y) * 0.5f)), p_icon_col, p_icon);
    }

    const float text_x = icon1.x + pad_x;
    const float text_max_x = p1.x - pad_x;
    const float name_y = ImFloor(p0.y + (row_h - (line_h * 2.0f + ui.asset_browser.item_subtitle_gap)) * 0.5f);
    const float sub_y = name_y + line_h + ui.asset_browser.item_subtitle_gap;

    if (renaming) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ui.outliner.rename_pad_x, 0.0f));
        ImGui::SetCursorScreenPos(ImVec2(text_x - ui.outliner.rename_pad_x, name_y));
        ImGui::SetNextItemWidth(ImMax(1.0f, text_max_x - text_x));
        if (rename_focus) {
            ImGui::SetKeyboardFocusHere();
            rename_focus = false;
        }
        const bool enter = ImGui::InputText("##rename", rename_buf, sizeof(rename_buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar();
        if (enter || ImGui::IsItemDeactivated()) {
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false)) _rename_commit();
            rename_target.clear();
        }
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    dl->PushClipRect(ImVec2(text_x, p0.y), ImVec2(text_max_x, p1.y), true);
    if (!renaming) dl->AddText(ImVec2(text_x, name_y), ImGui::GetColorU32(ImGuiCol_Text), p_name);
    if (p_type && *p_type) dl->AddText(ImVec2(text_x, sub_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), p_type);
    dl->PopClipRect();

    if (p_progress >= 0.0f && p_progress < 1.0f) {
        const float bar_h = ui.asset_browser.progress_height;
        const ImVec2 bar0(p0.x, p1.y - bar_h);
        const ImVec2 bar1(p1.x, p1.y);
        dl->AddRectFilled(bar0, bar1, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
        const float w = (bar1.x - bar0.x) * p_progress;
        if (w > 0.5f) dl->AddRectFilled(bar0, ImVec2(bar0.x + w, bar1.y), ImGui::GetColorU32(ImGuiCol_Button));
    }

    return result;
}

void ContentBrowserPanel::_list_draw(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    const float row_h = _row_height();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    if (_selection_folder != selected_folder) {
        _clear_selection();
        _selection_folder = selected_folder;
    }

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

        if (_scroll_to_rename && !rename_target.empty()) {
            for (uint32_t k = 0; k < (uint32_t)_visible.size(); ++k) {
                if (dir->entries[_visible[k]].path != rename_target) continue;
                const float y = (float)k * row_h;
                const float view_h = ImGui::GetWindowHeight();
                if (y < ImGui::GetScrollY() || y + row_h > ImGui::GetScrollY() + view_h) ImGui::SetScrollY(y - (view_h - row_h) * 0.5f);
                _scroll_to_rename = false;
                break;
            }
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)_visible.size(), row_h);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                Entry& e = dir->entries[_visible[row]];
                ImGui::PushID(e.lower_name.c_str());

                const float progress = importing ? ctx.imports->progress(e.path) : -1.0f;

                VkDescriptorSet set = VK_NULL_HANDLE;
                if (!e.is_dir) {
                    VkImageView view = VK_NULL_HANDLE;
                    if (!e.guid_checked) {
                        const AssetInfo info = _resolve_asset(e.path);
                        e.guid = info.guid;
                        e.asset_type = info.type;
                        e.guid_checked = true;
                    }
                    if (e.is_texture && ctx.renderer) {
                        if (e.asset_type == AssetType::TEXTURE) {
                            if (const LTexture* bt = ctx.renderer->textures.get(e.guid)) view = _thumb_view(ctx, e.guid, bt->image);
                        }
                    } else if (ctx.thumbnails) {
                        view = ctx.thumbnails->get(ctx, e.guid, e.asset_type);
                    }
                    if (view) set = ctx.imgui->texture_cache.get(view);
                }

                const ImU32 icon_col = e.is_dir ? ui.color.folder : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                const ItemResult r = _list_item((ImTextureID)set, e.icon, icon_col, e.name.c_str(), e.type.c_str(), e.path, progress);
                if (r.clicked) {
                    const ImGuiIO& io = ImGui::GetIO();
                    _click(*dir, (uint32_t)row, io.KeyCtrl, io.KeyShift);
                }

                if (e.is_dir) _drop_target(e.path);

                if (r.activated && e.is_dir) open_request = e.path;
                if (r.activated && !e.is_dir && e.path.extension() == ".lmodel") {
                    const AssetInfo info = read_asset_info(e.path);
                    if (info.valid() && info.type == AssetType::MODEL) ctx.tabs->open<ModelTab>(info.guid)->path = e.path;
                }
                if (r.activated && !e.is_dir && e.path.extension() == ".lmaterial") {
                    const AssetInfo info = read_asset_info(e.path);
                    if (info.valid() && info.type == AssetType::MATERIAL) ctx.tabs->open<MaterialTab>(info.guid)->path = e.path;
                }
                if (r.activated && !e.is_dir && e.is_texture && ctx.texture_viewer && e.asset_type == AssetType::TEXTURE && !(e.guid == Guid{})) ctx.texture_viewer->open(e.guid);
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
                _list_item(0, ICON_FA_FILE_IMPORT, ImGui::GetColorU32(ImGuiCol_TextDisabled), ppath.stem().string().c_str(), "Importing", ppath, ctx.imports->progress(ppath), true);
                ImGui::PopID();
                ++i;
            }
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) _clear_selection();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ui.outliner.row_pad_y * 2.0f));
        dock_menu_push_style();
        if (ImGui::BeginPopupContextWindow("##browser_empty", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            _context_menu(ctx, nullptr);
            ImGui::EndPopup();
        }
        dock_menu_pop_style();
        ImGui::PopStyleVar();

        if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && rename_target.empty()) {
            if (!selection.empty() && ImGui::IsKeyPressed(ImGuiKey_F2, false)) _rename_begin(selection.back());
            if (!selection.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) _delete_selection();
        }

        if (delete_requested) {
            delete_requested = false;
            _delete_now(ctx);
        }

        if (!cancel_request.empty()) {
            // ctx.imports->cancel(cancel_request);
            cancel_request.clear();
        }

        if (!open_request.empty()) selected_folder = open_request;
    }
    
    ImGui::PopStyleVar();
}

/**************/
/**** DRAW ****/
/**************/

void ContentBrowserPanel::_reset_root(const std::filesystem::path& p_root)
{
    _root = p_root;
    dirs.clear();
    _thumb_infos.clear();
    _clear_selection();
    _selection_folder.clear();
    rename_target.clear();
    cancel_request.clear();
    delete_requested = false;
    delete_confirm_open = false;
    selected_folder = p_root;
    last_refresh = -1.0e9;
}

VkImageView ContentBrowserPanel::thumbnail(EditorContext& ctx, Guid p_guid, AssetType p_type)
{
    if (p_guid == Guid{}) return VK_NULL_HANDLE;
    if (p_type == AssetType::TEXTURE) {
        if (!ctx.renderer) return VK_NULL_HANDLE;
        const LTexture* tex = ctx.renderer->textures.get(p_guid);
        return tex ? _thumb_view(ctx, p_guid, tex->image) : VK_NULL_HANDLE;
    }
    return ctx.thumbnails ? ctx.thumbnails->get(ctx, p_guid, p_type) : VK_NULL_HANDLE;
}

VkImageView ContentBrowserPanel::_thumb_view(EditorContext& ctx, Guid p_guid, const drivers::DeviceDriverVulkan::Image& p_image)
{
    const uint32_t channels = TextureViewer::channel_count(p_image.format);
    if (channels > 2) return p_image.image_view;

    auto it = _thumb_views.find(p_guid);
    if (it != _thumb_views.end()) {
        if (it->second.image == p_image.image) return it->second.view;
        ctx.imgui->texture_cache.retire(it->second.view, ctx.renderer->frame_number, ctx.renderer->frame_count);
        _thumb_retired.push_back({ it->second.view, ctx.renderer->frame_number });
        _thumb_views.erase(it);
    }

    VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ci.image = p_image.image;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci.format = p_image.format;
    ci.components = TextureViewer::swizzle(TextureViewer::Channel::RGB, channels);
    ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, p_image.mip_levels, 0, 1 };
    _thumb_device = ctx.renderer->dd->device;
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(_thumb_device, &ci, nullptr, &view) != VK_SUCCESS) return p_image.image_view;
    _thumb_views[p_guid] = ThumbView{ p_image.image, view };
    return view;
}

void ContentBrowserPanel::_thumb_collect(EditorContext& ctx)
{
    if (!ctx.renderer || !ctx.imgui) return;
    const uint64_t frame = ctx.renderer->frame_number;
    const uint32_t frames = ctx.renderer->frame_count;

    for (auto it = _thumb_views.begin(); it != _thumb_views.end();) {
        const LTexture* tex = ctx.renderer->textures.get(it->first);
        if (tex && tex->image.image == it->second.image) {
            ++it;
            continue;
        }
        ctx.imgui->texture_cache.retire(it->second.view, frame, frames);
        _thumb_retired.push_back({ it->second.view, frame });
        it = _thumb_views.erase(it);
    }

    for (size_t i = 0; i < _thumb_retired.size();) {
        if (frame >= _thumb_retired[i].frame + frames) {
            vkDestroyImageView(_thumb_device, _thumb_retired[i].view, nullptr);
            _thumb_retired[i] = _thumb_retired.back();
            _thumb_retired.pop_back();
        } else {
            ++i;
        }
    }
}

void ContentBrowserPanel::shutdown()
{
    if (_thumb_device == VK_NULL_HANDLE) return;
    for (auto& [guid, t] : _thumb_views) vkDestroyImageView(_thumb_device, t.view, nullptr);
    for (const ThumbRetired& r : _thumb_retired) vkDestroyImageView(_thumb_device, r.view, nullptr);
    _thumb_views.clear();
    _thumb_retired.clear();
}

void ContentBrowserPanel::draw_contents(EditorContext& ctx)
{
    registry = ctx.assets;
    _ctx = &ctx;
    if (_root != ctx.project->assets_dir) _reset_root(ctx.project->assets_dir);
    if (selected_folder.empty()) selected_folder = ctx.project->assets_dir;
    const std::filesystem::path& root = ctx.project->assets_dir;
    _cache_tick(ImGui::GetTime());
    _thumb_collect(ctx);
    
    const ImVec2 region_p0 = ImGui::GetCursorScreenPos();
    const UiStyle& ui = ui_style();
    const float thick = ui.asset_browser.splitter;
    ImVec2 avail = ImGui::GetContentRegionAvail();

    float body_w = avail.x - thick;
    if (body_w < 1.0f) body_w = 1.0f;
    const float min_side = ui.asset_browser.min_side;
    float left_w = ImFloor(ImClamp(body_w * 0.18f, ImMin(min_side, body_w * 0.5f), body_w - ImMin(min_side, body_w * 0.5f)));
    float right_w = body_w - left_w;

    ImGui::BeginChild("##left", ImVec2(left_w, avail.y), ImGuiChildFlags_AlwaysUseWindowPadding);
    if (_cache_get(root).exists) _draw_folder_node(root, 0);
    {
        const float divider_x = region_p0.x + left_w;
        const float shadow_w = ui.asset_browser.divider_shadow;
        const ImU32 c_edge = ui.color.shadow;
        const ImU32 c_fade = ui.color.transparent;
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(ImVec2(divider_x - shadow_w, region_p0.y), ImVec2(divider_x, region_p0.y + avail.y), c_fade, c_edge, c_edge, c_fade);
    }
    ImGui::EndChild();

    ImGui::SameLine(0, thick);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.panel);
    ImGui::BeginChild("##right", ImVec2(right_w, avail.y), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    const float pad_y = ImGui::GetStyle().WindowPadding.y;
    {
        const float pad_x = ui.outliner.pad_x;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad_y);
        const float bw = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
        dock_search_bar("##search", search_buf, sizeof(search_buf), -(bw * 3.0f + spacing * 3.0f + pad_x));

        ImGui::PushStyleColor(ImGuiCol_Button, ui.color.transparent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui.color.transparent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ui.color.transparent);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FILE_IMPORT, ImVec2(bw, bw))) _import(ctx);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Import asset");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SQUARE_PLUS, ImVec2(bw, bw))) ImGui::OpenPopup("##add_asset");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Add asset");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_PLUS, ImVec2(bw, bw))) _new_folder(ctx);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Create folder (moves the selection into it)");
        ImGui::PopStyleColor(3);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ui.outliner.row_pad_y * 2.0f));
        dock_menu_push_style();
        if (ImGui::BeginPopup("##add_asset")) {
            if (ImGui::MenuItem(ICON_FA_CUBES "  Model")) {
                const std::filesystem::path parent = selected_folder.empty() ? ctx.project->assets_dir : selected_folder;
                const std::filesystem::path path = _unique_path(parent, "New Model", ".lmodel");
                LModel model;
                model.guid = Guid::generate();
                if (model.save(path) != Error::OK) {
                    log_write("Content Browser: failed to create '%s'.", path.string().c_str());
                    return;
                }
                if (ctx.renderer) ctx.renderer->models.load(path);
                _select_only(path);
                _rename_begin(path);
                _scroll_to_rename = true;
                request_refresh();
            }
            if (ImGui::MenuItem(ICON_FA_PALETTE "  Material")) {
                const std::filesystem::path parent = selected_folder.empty() ? ctx.project->assets_dir : selected_folder;
                const std::filesystem::path path = _unique_path(parent, "New Material", ".lmaterial");
                LMaterial material;
                material.guid = Guid::generate();
                if (material.save(path) != Error::OK) {
                    log_write("Content Browser: failed to create '%s'.", path.string().c_str());
                    return;
                }
                if (ctx.renderer) ctx.renderer->materials.load(path);
                _select_only(path);
                _rename_begin(path);
                _scroll_to_rename = true;
                request_refresh();
            }
            ImGui::EndPopup();
        }
        dock_menu_pop_style();
        ImGui::PopStyleVar();
    }
    const float footer_h = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + pad_y;
    ImGui::BeginChild("##bottom_right", ImVec2(0, -footer_h), ImGuiChildFlags_None);
    _list_draw(ctx);
    ImGui::EndChild();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui_style().outliner.pad_x);
    ImGui::TextDisabled("%u items (%u selected)", selected_folder.empty() ? 0u : (uint32_t)_cache_get(selected_folder).entries.size(), (uint32_t)selection.size());
    ImGui::EndChild();

    _draw_delete_confirm(ctx);
}

}
