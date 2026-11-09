#include <editor/tabs/asset_manager/asset_manager.h>
#include <core/assets/lblendspace.h>
#include <core/assets/lmaterial.h>
#include <core/assets/lmodel.h>
#include <core/assets/loverlay.h>
#include <core/assets/lterrain.h>
#include <core/assets/lworld.h>
#include <core/base/error.h>
#include <core/io/file_io.h>
#include <core/io/path.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <drivers/windows/dialogs_win32.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/assets/import/import_dialog.h>
#include <editor/assets/import/reimport.h>
#include <editor/assets/references_dialog.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <toml++/toml.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <sstream>

namespace lumen {

struct NewAssetDesc {
    const char* icon;
    const char* label;
    const char* stem;
    const char* ext;
};

static const NewAssetDesc NEW_ASSETS[] = {
    { ICON_FA_EARTH_AMERICAS, "World", "New World", ".lworld" },
    { ICON_FA_MOUNTAIN_SUN, "Terrain", "New Terrain", ".lterrain" },
    { ICON_FA_CUBES, "Model", "New Model", ".lmodel" },
    { ICON_FA_PALETTE, "Material", "New Material", ".lmaterial" },
    { ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT, "Blend Space", "New Blend Space", ".lblendspace" },
    { ICON_FA_LAYER_GROUP, "Overlay", "New Overlay", ".loverlay" },
};
static_assert(std::size(NEW_ASSETS) == (size_t)AssetManager::NewAsset::COUNT);

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
    return ext == ".lmodel" || ext == ".lmaterial" || ext == ".lblendspace" || ext == ".loverlay" || ext == ".lworld";
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

/**************/
/**** DRAW ****/
/**************/

void AssetManager::draw(EditorContext& ctx)
{
    prepare(ctx);

    const UiStyle& ui = ui_style();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float thick = ui.asset_browser.splitter;
    const float body_w = ImMax(1.0f, avail.x - thick * 2.0f);
    const float min_side = ImMin(ui.asset_browser.min_side, body_w / 3.0f);
    const float tree_w = ImFloor(ImClamp(body_w * split_x, min_side, body_w - min_side * 2.0f));
    const float files_w = ImFloor(ImClamp(body_w * split_files, min_side, body_w - tree_w - min_side));
    const float side_w = ImMax(1.0f, body_w - tree_w - files_w);

    draw_tree(origin, tree_w, avail.y, false);
    ImGui::SameLine(0, thick);
    draw_items_column(ctx, ImVec2(files_w, avail.y));
    ImGui::SameLine(0, thick);
    _draw_side(ImVec2(side_w, avail.y));
}

void AssetManager::_draw_side(ImVec2 p_size)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui_style().color.panel);
    ImGui::BeginChild("##side", p_size, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void AssetManager::_reset(const std::filesystem::path& p_root)
{
    AssetBrowser::_reset(p_root);
    rename_target.clear();
    cancel_request.clear();
    delete_requested = false;
}

void AssetManager::_draw_toolbar(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    const float pad_x = ui.outliner.pad_x;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);

    const char* label = ICON_FA_FILTER "  Common Assets";
    const float bw = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ui_search_bar("##search", search_buf, sizeof(search_buf), -(bw + spacing + pad_x));
    ImGui::SameLine();
    ui_push_buttons(UiButton::PRIMARY);
    if (ImGui::Button(label, ImVec2(bw, 0.0f))) ImGui::OpenPopup("##common_assets");
    ui_pop_buttons();

    const ImVec2 bmax = ImGui::GetItemRectMax();
    ImGui::SetNextWindowPos(ImVec2(bmax.x, bmax.y + ui.menu.popup_offset), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    ui_context_menu_push_style();
    if (ImGui::BeginPopup("##common_assets")) {
        _common_assets_menu(ctx);
        ImGui::EndPopup();
    }
    ui_context_menu_pop_style();
}

ImVec2 AssetManager::_item_size(float p_avail_w) const
{
    const UiStyle& ui = ui_style();
    return ImVec2(p_avail_w, ImFloor(ImGui::GetTextLineHeight() * 2.0f + ui.asset_browser.item_subtitle_gap + ui.asset_browser.list_row_pad * 2.0f));
}

AssetBrowser::ItemResult AssetManager::_draw_item(EditorContext& ctx, const Item& p_item, ImVec2 p_size)
{
    const UiStyle& ui = ui_style();
    const float pad = ui.asset_browser.card_pad;
    const float row_pad = ui.asset_browser.list_row_pad;
    const std::filesystem::path& path = *p_item.path;
    const bool renaming = rename_target == path;
    const bool selected = _is_selected(path);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + p_size.x, p0.y + p_size.y);

    const bool cancellable = p_item.progress >= 0.0f && p_item.progress < 1.0f;
    const float cs = ImFloor(ImGui::GetFontSize() + ui.dock_tab.close_size_pad);
    const ImVec2 ca(ImFloor(p1.x - cs - pad), ImFloor(p0.y + (p_size.y - cs) * 0.5f));
    const ImVec2 cb(ca.x + cs, ca.y + cs);
    bool close_hot = false;
    if (cancellable) {
        ImGui::SetCursorScreenPos(ca);
        if (ImGui::InvisibleButton("##cancel_import", ImVec2(cs, cs))) cancel_request = path;
        close_hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Cancel import");
        ImGui::SetCursorScreenPos(p0);
    }

    ItemResult result;
    ImGui::SetNextItemAllowOverlap();
    result.clicked = ImGui::InvisibleButton("##row", p_size);
    const bool hovered = ImGui::IsItemHovered();
    const bool context = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseReleased(ImGuiMouseButton_Right));
    result.activated = !renaming && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (context && !selected && !p_item.importing) _select_only(path);

    ui_context_menu_push_style();
    if (!renaming && !p_item.importing && ImGui::BeginPopupContextItem("##row_menu")) {
        _context_menu(ctx, path);
        ImGui::EndPopup();
    }
    ui_context_menu_pop_style();

    if (!renaming && !p_item.importing) _drag_source(p_item);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool hot = hovered || close_hot;
    if (selected || hot) dl->AddRectFilled(p0, p1, selected ? ImGui::GetColorU32(hot ? ImGuiCol_HeaderHovered : ImGuiCol_Header) : ui.color.field_hover);

    const float side = p_size.y;
    _draw_thumb(dl, p_item, ImVec2(p0.x + row_pad, p0.y + row_pad), ImVec2(p0.x + side - row_pad, p1.y - row_pad), ImFloor(side * ui.asset_browser.list_icon_scale));

    const float name_x = p0.x + side + pad;
    const float name_max_x = cancellable ? ca.x - pad : p1.x - pad;
    const float name_y = p0.y + row_pad;
    const float type_y = name_y + ImGui::GetTextLineHeight() + ui.asset_browser.item_subtitle_gap;
    if (renaming) {
        const UiRename rename = ui_inline_rename(ImVec2(name_x, name_y), ImMax(1.0f, name_max_x - name_x), rename_buf, sizeof(rename_buf), rename_focus);
        if (rename != UiRename::NONE) {
            if (rename == UiRename::COMMIT) _rename_commit();
            rename_target.clear();
        }
    }
    dl->PushClipRect(ImVec2(name_x, p0.y), ImVec2(name_max_x, p1.y), true);
    if (!renaming) dl->AddText(ImVec2(name_x, name_y), ImGui::GetColorU32(ImGuiCol_Text), p_item.name);
    if (p_item.subtitle && *p_item.subtitle) dl->AddText(ImVec2(name_x, type_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), p_item.subtitle);
    dl->PopClipRect();

    _draw_progress(dl, p_item, p0, p1);

    if (cancellable) {
        const ImVec2 c(ca.x + cs * 0.5f, ca.y + cs * 0.5f);
        const float r = ImFloor(cs * ui.dock_tab.close_glyph_scale) + 0.5f;
        if (close_hot) dl->AddRectFilled(ca, cb, ui.color.line, ui.dock_tab.close_rounding);
        const ImU32 xc = close_hot ? ui.color.text : ui.color.text_dim;
        dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), xc, ui.dock_tab.close_glyph_stroke);
        dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), xc, ui.dock_tab.close_glyph_stroke);
    }
    return result;
}

void AssetManager::_tree_row_hook(const Dir& p_dir)
{
    _drop_target(p_dir.path);
}

void AssetManager::_item_hook(const Entry& p_entry)
{
    if (p_entry.is_dir) _drop_target(p_entry.path);
}

void AssetManager::_items_end(EditorContext& ctx)
{
    ui_context_menu_push_style();
    if (ImGui::BeginPopupContextWindow("##browser_empty", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        _common_assets_menu(ctx);
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
        ctx.imports->cancel(cancel_request);
        cancel_request.clear();
    }
}

/*****************/
/**** EDITING ****/
/*****************/

std::filesystem::path AssetManager::_target_folder(const EditorContext& ctx) const
{
    return selected_folder.empty() ? ctx.project->assets_dir : selected_folder;
}

bool AssetManager::_nested_in_selection(const std::filesystem::path& p_path) const
{
    for (const std::filesystem::path& o : selection) {
        if (o != p_path && Paths::is_under(p_path, o)) return true;
    }
    return false;
}

void AssetManager::_begin_new(const std::filesystem::path& p_path)
{
    _select_only(p_path);
    _rename_begin(p_path);
    _scroll_target = p_path;
    request_refresh();
}

void AssetManager::_rename_begin(const std::filesystem::path& p_path)
{
    rename_target = p_path;
    std::error_code ec;
    std::snprintf(rename_buf, sizeof(rename_buf), "%s", std::filesystem::is_directory(p_path, ec) ? p_path.filename().string().c_str() : p_path.stem().string().c_str());
    rename_focus = true;
}

void AssetManager::_rename_commit()
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

void AssetManager::_new_folder(EditorContext& ctx)
{
    std::error_code ec;
    const std::filesystem::path path = _unique_path(_target_folder(ctx), "New Folder", "");
    if (!std::filesystem::create_directory(path, ec)) return;
    if (!selection.empty()) _move_selection(path);
    _begin_new(path);
}

void AssetManager::_duplicate_selection(EditorContext& ctx)
{
    std::vector<std::filesystem::path> created;
    for (const std::filesystem::path& p : selection) {
        if (!_duplicable(p)) continue;
        const std::filesystem::path dst = _duplicate_asset(p);
        if (dst.empty()) {
            log_write("Asset Manager: failed to duplicate '%s'.", p.string().c_str());
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

void AssetManager::_import(EditorContext& ctx)
{
    std::error_code ec;
    if (selected_folder.empty() || !std::filesystem::exists(selected_folder, ec)) return;
    std::vector<std::wstring> files = drivers::Win32Dialogs::open_files(
        L"All Supported\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.fbx;*.gltf;*.glb;*.obj;*.dae;*.stl;*.ply;*.3ds;*.blend\0"
        L"Models\0*.fbx;*.gltf;*.glb;*.obj;*.dae;*.stl;*.ply;*.3ds;*.blend\0"
        L"Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0"
        L"All Files\0*.*\0"
    );
    for (const std::wstring& f : files) {
        const std::filesystem::path source = f;
        if (!ctx.import_dialog->push(ctx, source, selected_folder)) log_write("Skipped unsupported import: %s", source.string().c_str());
    }
}

void AssetManager::_move_selection(const std::filesystem::path& p_dst)
{
    for (const std::filesystem::path& p : selection) {
        if (_nested_in_selection(p) || p == p_dst || Paths::is_under(p_dst, p)) continue;
        Paths::move(p, p_dst);
    }
    _clear_selection();
    _thumb_infos.clear();
    request_refresh();
}

void AssetManager::_drop_target(const std::filesystem::path& p_dst)
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

void AssetManager::_delete_content(EditorContext& ctx, const std::filesystem::path& p_asset)
{
    AssetInfo info = read_asset_info(p_asset);
    if (!info.valid()) return;

    if (ctx.renderer) {
        switch (info.type) {
            case AssetType::TEXTURE: ctx.renderer->textures.unload(info.guid); break;
            case AssetType::MESH: ctx.renderer->geometry.unload(info.guid); break;
            case AssetType::MODEL: ctx.renderer->models.unload(info.guid); break;
            case AssetType::MATERIAL: ctx.renderer->materials.unload(info.guid); break;
            case AssetType::SKELETON: ctx.renderer->skeletons.unload(info.guid); break;
            default: break;
        }
    }

    ctx.project->remove_content(info.guid);
}

void AssetManager::_delete_asset(EditorContext& ctx, const std::filesystem::path& p_path)
{
    _delete_content(ctx, p_path);
    Paths::remove_to_recycle(p_path);
}

void AssetManager::_delete_folder(EditorContext& ctx, const std::filesystem::path& p_folder)
{
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(p_folder, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec)) continue;
        const std::filesystem::path ext = it->path().extension();
        if (ext == ".ltexture" || ext == ".lmesh" || ext == ".lmodel" || ext == ".lmaterial" || ext == ".lskeleton" || ext == ".lanim") _delete_content(ctx, it->path());
    }
    Paths::remove_to_recycle(p_folder);
}

void AssetManager::_delete_folders_only()
{
    std::error_code ec;
    for (const std::filesystem::path& p : selection) {
        if (!std::filesystem::is_directory(p, ec)) continue;
        std::vector<std::filesystem::path> children;
        for (auto it = std::filesystem::directory_iterator(p, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) children.push_back(it->path());
        for (const std::filesystem::path& child : children) Paths::move(child, p.parent_path());
        if (std::filesystem::is_empty(p, ec)) Paths::remove_to_recycle(p);
        else log_write("Asset Manager: '%s' kept, some items already exist in the parent folder.", p.string().c_str());
    }
    _clear_selection();
    _thumb_infos.clear();
    request_refresh();
}

void AssetManager::_delete_selection()
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

void AssetManager::_delete_now(EditorContext& ctx)
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

void AssetManager::_context_menu(EditorContext& ctx, const std::filesystem::path& p_row)
{
    std::error_code ec;
    const uint32_t count = (uint32_t)selection.size();
    bool any_folder = false;
    for (const std::filesystem::path& p : selection) any_folder = any_folder || std::filesystem::is_directory(p, ec);

    if (ImGui::MenuItem(ICON_FA_PEN "  Rename", "F2")) _rename_begin(p_row);
    if (_duplicable(p_row) && ImGui::MenuItem(ICON_FA_COPY "  Duplicate")) _duplicate_selection(ctx);
    if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS "  Move To New Folder")) _new_folder(ctx);
    if (!any_folder) {
        const bool reimportable = std::any_of(selection.begin(), selection.end(), [](const std::filesystem::path& p) { return can_reimport(p); });
        if (reimportable) {
            ImGui::Separator();
            const bool has_settings = std::any_of(selection.begin(), selection.end(), [](const std::filesystem::path& p) { return reimport_has_settings(reimport_target(p).kind); });
            if (ImGui::MenuItem(ICON_FA_ROTATE "  Reimport")) reimport_assets(ctx, selection);
            if (ImGui::MenuItem(ICON_FA_SLIDERS "  Reimport With Settings...", nullptr, false, has_settings)) reimport_with_settings(ctx, selection);
        }
    }
    const AssetInfo info = count == 1 && !any_folder ? read_asset_info(p_row) : AssetInfo{};
    if (info.valid() && ctx.references_dialog) {
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_MAGNIFYING_GLASS "  Find References")) ctx.references_dialog->open(ctx, info.guid, ReferencesDialog::Mode::FIND);
        if (ImGui::MenuItem(ICON_FA_RIGHT_LEFT "  Replace References...")) ctx.references_dialog->open(ctx, info.guid, ReferencesDialog::Mode::REPLACE);
    }
    ImGui::Separator();
    char del[48];
    std::snprintf(del, sizeof(del), count > 1 ? ICON_FA_TRASH_CAN "  Delete %u Items" : ICON_FA_TRASH_CAN "  Delete", count);
    if (ImGui::MenuItem(del, "Del")) _delete_selection();
    if (any_folder) {
        if (ImGui::MenuItem(ICON_FA_FOLDER_MINUS "  Delete Folder Only")) _delete_folders_only();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Removes the folder and moves its contents to the parent folder");
    }
}

void AssetManager::_create_asset(EditorContext& ctx, NewAsset p_kind)
{
    const NewAssetDesc& d = NEW_ASSETS[(size_t)p_kind];
    const std::filesystem::path path = _unique_path(_target_folder(ctx), d.stem, d.ext);
    Error err = Error::FAILED;
    switch (p_kind) {
        case NewAsset::WORLD: { LWorld a; a.guid = Guid::generate(); err = a.save(path); } break;
        case NewAsset::TERRAIN: { LTerrain a; a.guid = Guid::generate(); err = a.save(path); } break;
        case NewAsset::MODEL: { LModel a; a.guid = Guid::generate(); err = a.save(path); } break;
        case NewAsset::MATERIAL: { LMaterial a; a.guid = Guid::generate(); err = a.save(path); } break;
        case NewAsset::BLEND_SPACE: { LBlendSpace a; a.guid = Guid::generate(); err = a.save(path); } break;
        case NewAsset::OVERLAY: { LOverlay a; a.guid = Guid::generate(); err = a.save(path); } break;
        case NewAsset::COUNT: break;
    }
    if (err != Error::OK) {
        log_write("Asset Manager: failed to create '%s'.", path.string().c_str());
        return;
    }
    if (ctx.renderer && p_kind == NewAsset::MODEL) ctx.renderer->models.load(path);
    if (ctx.renderer && p_kind == NewAsset::MATERIAL) ctx.renderer->materials.load(path);
    _begin_new(path);
}

void AssetManager::_add_asset_menu(EditorContext& ctx)
{
    char label[64];
    for (size_t i = 0; i < (size_t)NewAsset::COUNT; ++i) {
        std::snprintf(label, sizeof(label), "%s  %s", NEW_ASSETS[i].icon, NEW_ASSETS[i].label);
        if (ImGui::MenuItem(label)) _create_asset(ctx, (NewAsset)i);
    }
}

void AssetManager::_common_assets_menu(EditorContext& ctx)
{
    if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS "  New Folder")) {
        _clear_selection();
        _new_folder(ctx);
    }
    if (ImGui::MenuItem(ICON_FA_FILE_IMPORT "  Import...")) _import(ctx);
    ImGui::Separator();
    _add_asset_menu(ctx);
}

}
