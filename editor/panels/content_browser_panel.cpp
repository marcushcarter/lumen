#include <editor/panels/content_browser_panel.h>
#include <core/assets/lmaterial.h>
#include <core/assets/lmodel.h>
#include <core/base/error.h>
#include <core/base/strings.h>
#include <core/io/file_io.h>
#include <core/io/path.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <drivers/imgui/imgui_driver.h>
#include <drivers/windows/dialogs_win32.h>
#include <editor/assets/import/asset_import_any.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/assets/texture_viewer.h>
#include <editor/assets/thumbnail_cache.h>
#include <editor/tabs/material/material_tab.h>
#include <editor/tabs/model/model_tab.h>
#include <editor/tabs/tab_manager.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <toml++/toml.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace lumen {

/***************/
/**** CACHE ****/
/***************/

static std::string _type_name(std::string p_ext)
{
    str_lower(p_ext);
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
    r_dir.path = p_dir;
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
        e.lower_name = str_lowered(e.path.filename().string());
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

void ContentBrowserPanel::_tree_emit(const std::filesystem::path& p_dir, uint16_t p_depth)
{
    const Dir& node = _cache_get(p_dir);
    if (!node.exists) return;
    tree_rows.push_back({ &node, p_depth });
    if (p_depth != 0 && !open_folders.contains(p_dir)) return;
    if (tree_ascending) {
        for (const std::filesystem::path& sub : node.subdirs) _tree_emit(sub, (uint16_t)(p_depth + 1));
    } else {
        for (auto it = node.subdirs.rbegin(); it != node.subdirs.rend(); ++it) _tree_emit(*it, (uint16_t)(p_depth + 1));
    }
}

void ContentBrowserPanel::_tree_toggle(const std::filesystem::path& p_dir)
{
    if (!open_folders.erase(p_dir)) open_folders.insert(p_dir);
}

void ContentBrowserPanel::_draw_tree_row(const TreeRow& p_row, float p_row_h)
{
    const Dir& node = *p_row.dir;
    const bool is_root = p_row.depth == 0;
    const bool has_children = !node.subdirs.empty();
    const bool open = has_children && (is_root || open_folders.contains(node.path));
    const bool selected = selected_folder == node.path;
    const UiStyle& ui = ui_style();
    const float font = ImGui::GetFontSize();
    const float content_h = p_row_h - ui.outliner.row_pad_y * 2.0f;

    ImGui::TableNextRow(ImGuiTableRowFlags_None, p_row_h);
    ImGui::PushID(node.path_str.c_str());
    ImGui::TableSetColumnIndex(0);
    const ImVec2 cell = ImGui::GetCursorScreenPos();
    const float text_y = cell.y + ImFloor((content_h - font) * 0.5f);
    if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, content_h))) {
        selected_folder = node.path;
        if (!is_root && has_children && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) _tree_toggle(node.path);
    }
    _drop_target(node.path);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = cell.x + (float)p_row.depth * font;
    if (has_children) {
        bool arrow_hovered = false;
        if (!is_root) {
            ImGui::SetCursorScreenPos(ImVec2(x, cell.y));
            if (ImGui::InvisibleButton("##arrow", ImVec2(font, content_h))) _tree_toggle(node.path);
            arrow_hovered = ImGui::IsItemHovered();
        }
        const ImU32 arrow_col = ImGui::GetColorU32(arrow_hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImGui::RenderArrow(dl, ImVec2(x, text_y + font * ui.outliner.arrow_offset_scale), arrow_col, open ? ImGuiDir_Down : ImGuiDir_Right, ui.outliner.arrow_scale);
    }
    x += font;

    const char* icon = open ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;
    const float icon_w = font * ui.outliner.icon_width_scale;
    const ImVec2 icon_size = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(ImFloor(x + (icon_w - icon_size.x) * 0.5f), text_y), ui.color.folder, icon);
    x += icon_w + ui.outliner.icon_gap;
    dl->AddText(ImVec2(x, text_y), ImGui::GetColorU32(ImGuiCol_Text), node.name.c_str());

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
        if (ext == ".ltexture" || ext == ".lmesh" || ext == ".lmodel" || ext == ".lmaterial") _delete_content(ctx, it->path());
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

std::filesystem::path ContentBrowserPanel::_target_folder(const EditorContext& ctx) const
{
    return selected_folder.empty() ? ctx.project->assets_dir : selected_folder;
}

bool ContentBrowserPanel::_nested_in_selection(const std::filesystem::path& p_path) const
{
    for (const std::filesystem::path& o : selection) {
        if (o != p_path && Paths::is_under(p_path, o)) return true;
    }
    return false;
}

void ContentBrowserPanel::_begin_new(const std::filesystem::path& p_path)
{
    _select_only(p_path);
    _rename_begin(p_path);
    _scroll_to_rename = true;
    request_refresh();
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
    if (created.size() == 1) {
        _begin_new(created.front());
        return;
    }
    selection = created;
    anchor = created.back();
    request_refresh();
}

void ContentBrowserPanel::_new_folder(EditorContext& ctx)
{
    std::error_code ec;
    const std::filesystem::path path = _unique_path(_target_folder(ctx), "New Folder", "");
    if (!std::filesystem::create_directory(path, ec)) return;
    if (!selection.empty()) _move_selection(path);
    _begin_new(path);
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
        if (_nested_in_selection(p) || p == p_dst || Paths::is_under(p_dst, p)) continue;
        Paths::move(p, p_dst);
    }
    _clear_selection();
    _thumb_infos.clear();
    request_refresh();
}

void ContentBrowserPanel::_delete_folders_only()
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
    std::snprintf(del, sizeof(del), count > 1 ? ICON_FA_TRASH_CAN "  Delete %u Items" : ICON_FA_TRASH_CAN "  Delete", count);
    if (ImGui::MenuItem(del, "Del")) _delete_selection();
    if (any_folder) {
        if (ImGui::MenuItem(ICON_FA_FOLDER_MINUS "  Delete Folder Only")) _delete_folders_only();
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
        if (!std::filesystem::is_directory(p, ec) || _nested_in_selection(p)) continue;
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
    const std::wstring heading = folders == 1 ? L"Delete \"" + first->filename().wstring() + L"\" with " + std::to_wstring(contents) + L" items?" : L"Delete " + std::to_wstring(folders) + L" folders with " + std::to_wstring(contents) + L" items?";
    if (drivers::Win32Dialogs::confirm(folders == 1 ? L"Delete Folder" : L"Delete Folders", heading.c_str(), L"Deleted files go to the Recycle Bin.")) delete_requested = true;
}

void ContentBrowserPanel::_delete_now(EditorContext& ctx)
{
    std::error_code ec;
    for (const std::filesystem::path& p : selection) {
        if (_nested_in_selection(p) || !std::filesystem::exists(p, ec)) continue;
        if (std::filesystem::is_directory(p, ec)) _delete_folder(ctx, p);
        else _delete_asset(ctx, p);
    }
    _clear_selection();
    _thumb_infos.clear();
    request_refresh();
}

ContentBrowserPanel::ItemResult ContentBrowserPanel::_list_item(EditorContext& ctx, ImTextureID p_texture, const char* p_icon, ImU32 p_icon_col, const char* p_name, const char* p_type, const std::filesystem::path& p_path, float p_progress, bool p_importing)
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

    ui_context_menu_push_style();
    if (!renaming && ImGui::BeginPopupContextItem("##row_menu")) {
        if (p_importing) {
            if (ImGui::MenuItem("Cancel Import")) cancel_request = p_path;
        } else {
            _context_menu(ctx, &p_path);
        }
        ImGui::EndPopup();
    }
    ui_context_menu_pop_style();

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
        const UiRename rename = ui_inline_rename(ImVec2(text_x, name_y), ImMax(1.0f, text_max_x - text_x), rename_buf, sizeof(rename_buf), rename_focus);
        if (rename != UiRename::NONE) {
            if (rename == UiRename::COMMIT) _rename_commit();
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
        const std::string query = str_lowered(search_buf);

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
                    if (!e.guid_checked) {
                        const AssetInfo info = _resolve_asset(e.path);
                        e.guid = info.guid;
                        e.asset_type = info.type;
                        e.guid_checked = true;
                    }
                    const bool shown = !e.is_texture || e.asset_type == AssetType::TEXTURE;
                    const VkImageView view = shown ? ctx.thumbnails->get(ctx, e.guid, e.asset_type) : VK_NULL_HANDLE;
                    if (view) set = ctx.imgui->texture_cache.get(view);
                }

                const ImU32 icon_col = e.is_dir ? ui.color.folder : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                const ItemResult r = _list_item(ctx, (ImTextureID)set, e.icon, icon_col, e.name.c_str(), e.type.c_str(), e.path, progress);
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
                if (r.activated && !e.is_dir && e.is_texture && ctx.texture_viewer && e.asset_type == AssetType::TEXTURE && e.guid.valid()) ctx.texture_viewer->open(e.guid);
                ImGui::PopID();
            }
        }
        clipper.End();

        if (importing) {
            int i = (int)_visible.size();
            std::vector<std::filesystem::path> pending = ctx.imports->pending_out(selected_folder);
            for (const auto& ppath : pending) {
                if (!query.empty() && str_lowered(ppath.filename().string()).find(query) == std::string::npos) continue;

                ImGui::PushID(i);
                _list_item(ctx, 0, ICON_FA_FILE_IMPORT, ImGui::GetColorU32(ImGuiCol_TextDisabled), ppath.stem().string().c_str(), "Importing", ppath, ctx.imports->progress(ppath), true);
                ImGui::PopID();
                ++i;
            }
        }

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) _clear_selection();

        ui_context_menu_push_style();
        if (ImGui::BeginPopupContextWindow("##browser_empty", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            _context_menu(ctx, nullptr);
            ImGui::EndPopup();
        }
        ui_context_menu_pop_style();

        if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && rename_target.empty()) {
            if (!selection.empty() && ImGui::IsKeyPressed(ImGuiKey_F2, false)) _rename_begin(selection.back());
            if (!selection.empty() && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) _delete_selection();
        }

        if (delete_requested) {
            delete_requested = false;
            _delete_now(ctx);
        }

        if (!cancel_request.empty()) {
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
    tree_rows.clear();
    open_folders.clear();
    _thumb_infos.clear();
    _clear_selection();
    _selection_folder.clear();
    rename_target.clear();
    cancel_request.clear();
    delete_requested = false;
    selected_folder = p_root;
    last_refresh = -1.0e9;
}

void ContentBrowserPanel::_add_asset_menu(EditorContext& ctx)
{
    if (ImGui::MenuItem(ICON_FA_CUBES "  Model")) {
        const std::filesystem::path path = _unique_path(_target_folder(ctx), "New Model", ".lmodel");
        LModel model;
        model.guid = Guid::generate();
        if (model.save(path) == Error::OK) {
            if (ctx.renderer) ctx.renderer->models.load(path);
            _begin_new(path);
        } else {
            log_write("Content Browser: failed to create '%s'.", path.string().c_str());
        }
    }
    if (ImGui::MenuItem(ICON_FA_PALETTE "  Material")) {
        const std::filesystem::path path = _unique_path(_target_folder(ctx), "New Material", ".lmaterial");
        LMaterial material;
        material.guid = Guid::generate();
        if (material.save(path) == Error::OK) {
            if (ctx.renderer) ctx.renderer->materials.load(path);
            _begin_new(path);
        } else {
            log_write("Content Browser: failed to create '%s'.", path.string().c_str());
        }
    }
}

void ContentBrowserPanel::_draw_tree(const std::filesystem::path& p_root, ImVec2 p_origin, float p_width, float p_height)
{
    const UiStyle& ui = ui_style();
    const UiStyle::Outliner& style = ui.outliner;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, style.padding);
    ImGui::BeginChild("##left", ImVec2(p_width, p_height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    const float row_h = ImMax(ImGui::GetFrameHeight(), ImGui::GetFontSize() + style.row_pad_y * 2.0f);
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.cell_pad_x, style.row_pad_y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, style.row_pad_y * 2.0f));
    if (ImGui::BeginTable("##folders", 1, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Folder Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            if (specs->SpecsCount > 0) tree_ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
            specs->SpecsDirty = false;
        }

        tree_rows.clear();
        _tree_emit(p_root, 0);

        ImGuiListClipper clipper;
        clipper.Begin((int)tree_rows.size(), row_h);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) _draw_tree_row(tree_rows[i], row_h);
        }

        const float divider_x = p_origin.x + p_width;
        const float shadow_w = ui.asset_browser.divider_shadow;
        const ImU32 edge = ui.color.shadow;
        const ImU32 fade = ui.color.transparent;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(p_origin, ImVec2(divider_x, p_origin.y + p_height), false);
        dl->AddRectFilledMultiColor(ImVec2(divider_x - shadow_w, p_origin.y), ImVec2(divider_x, p_origin.y + p_height), fade, edge, edge, fade);
        dl->PopClipRect();
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
    ImGui::EndChild();
}

void ContentBrowserPanel::_draw_toolbar(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    const float pad_x = ui.outliner.pad_x;
    const float bw = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
    ui_search_bar("##search", search_buf, sizeof(search_buf), -(bw * 3.0f + spacing * 3.0f + pad_x));

    ui_push_buttons(UiButton::GHOST);
    ImGui::SameLine();
    if (ui_icon_button(ICON_FA_FILE_IMPORT, bw, "Import asset")) _import(ctx);
    ImGui::SameLine();
    if (ui_icon_button(ICON_FA_SQUARE_PLUS, bw, "Add asset")) ImGui::OpenPopup("##add_asset");
    ImGui::SameLine();
    if (ui_icon_button(ICON_FA_FOLDER_PLUS, bw, "Create folder (moves the selection into it)")) _new_folder(ctx);
    ui_pop_buttons();

    ui_context_menu_push_style();
    if (ImGui::BeginPopup("##add_asset")) {
        _add_asset_menu(ctx);
        ImGui::EndPopup();
    }
    ui_context_menu_pop_style();
}

void ContentBrowserPanel::_draw_footer()
{
    const uint32_t count = selected_folder.empty() ? 0u : (uint32_t)_cache_get(selected_folder).entries.size();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui_style().outliner.pad_x);
    ImGui::TextDisabled("%u items (%u selected)", count, (uint32_t)selection.size());
}

void ContentBrowserPanel::draw_contents(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    const std::filesystem::path& root = ctx.project->assets_dir;
    registry = ctx.assets;
    if (_root != root) _reset_root(root);
    if (selected_folder.empty()) selected_folder = root;
    _cache_tick(ImGui::GetTime());

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float thick = ui.asset_browser.splitter;
    const float body_w = ImMax(1.0f, avail.x - thick);
    const float min_side = ImMin(ui.asset_browser.min_side, body_w * 0.5f);
    const float left_w = ImFloor(ImClamp(body_w * split_x, min_side, body_w - min_side));
    const float right_w = body_w - left_w;

    _draw_tree(root, origin, left_w, avail.y);
    ImGui::SameLine(0, thick);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.panel);
    ImGui::BeginChild("##right", ImVec2(right_w, avail.y), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    _draw_toolbar(ctx);
    const float footer_h = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y + ImGui::GetStyle().WindowPadding.y;
    ImGui::BeginChild("##bottom_right", ImVec2(0, -footer_h), ImGuiChildFlags_None);
    _list_draw(ctx);
    ImGui::EndChild();
    _draw_footer();
    ImGui::EndChild();

}

}
