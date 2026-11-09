#include <editor/assets/asset_browser.h>
#include <core/base/strings.h>
#include <core/project/project.h>
#include <drivers/imgui/imgui_driver.h>
#include <editor/assets/import/asset_import_tracker.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/thumbnail_cache.h>
#include <editor/tabs/animation/animation_tab.h>
#include <editor/tabs/blend_space/blend_space_tab.h>
#include <editor/tabs/material/material_tab.h>
#include <editor/tabs/mesh/mesh_tab.h>
#include <editor/tabs/model/model_tab.h>
#include <editor/tabs/overlay/overlay_tab.h>
#include <editor/tabs/terrain/terrain_tab.h>
#include <editor/tabs/texture/texture_tab.h>
#include <editor/tabs/world/world_tab.h>
#include <editor/tabs/tab_manager.h>
#include <editor/ui/ui_style.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace lumen {

const char* AssetBrowser::icon_for(const std::filesystem::path& p_path, bool p_is_dir)
{
    if (p_is_dir) return ICON_FA_FOLDER;
    const std::filesystem::path ext = p_path.extension();
    if (ext == ".ltexture") return ICON_FA_IMAGE;
    if (ext == ".lmesh") return MeshTab::ICON;
    if (ext == ".lmodel") return ModelTab::ICON;
    if (ext == ".lmaterial") return MaterialTab::ICON;
    if (ext == ".lskeleton") return ICON_FA_BONE;
    if (ext == ".lanim") return AnimationTab::ICON;
    if (ext == ".lblendspace") return BlendSpaceTab::ICON;
    if (ext == ".loverlay") return OverlayTab::ICON;
    if (ext == ".lworld") return ICON_FA_EARTH_AMERICAS;
    if (ext == ".lterrain") return TerrainTab::ICON;
    return ICON_FA_FILE;
}

std::string AssetBrowser::type_name(std::string p_ext)
{
    str_lower(p_ext);
    if (p_ext == ".lmesh") return "Mesh";
    if (p_ext == ".ltexture") return "Texture";
    if (p_ext == ".lmodel") return "Model";
    if (p_ext == ".lmaterial") return "Material";
    if (p_ext == ".lskeleton") return "Skeleton";
    if (p_ext == ".lanim") return "Animation";
    if (p_ext == ".lblendspace") return "Blend Space";
    if (p_ext == ".loverlay") return "Overlay";
    if (p_ext == ".lworld") return "World";
    if (p_ext == ".lterrain") return "Terrain";
    if (!p_ext.empty() && p_ext[0] == '.') p_ext.erase(0, 1);
    for (char& c : p_ext) c = (char)std::toupper((unsigned char)c);
    return p_ext;
}

void AssetBrowser::prepare(EditorContext& ctx)
{
    const std::filesystem::path& assets = ctx.project->assets_dir;
    registry = ctx.assets;
    if (root != assets) _reset(assets);
    if (selected_folder.empty()) selected_folder = root;
    _cache_tick(ImGui::GetTime());
}

void AssetBrowser::_reset(const std::filesystem::path& p_root)
{
    root = p_root;
    dirs.clear();
    tree_rows.clear();
    open_folders.clear();
    _thumb_infos.clear();
    _clear_selection();
    _selection_folder.clear();
    _scroll_target.clear();
    selected_folder = p_root;
    last_refresh = -1.0e9;
}

/***************/
/**** CACHE ****/
/***************/

void AssetBrowser::_cache_tick(double p_now)
{
    if (!refresh_requested && p_now - last_refresh < CACHE_REFRESH_INTERVAL_S) return;
    dirs.clear();
    last_refresh = p_now;
    refresh_requested = false;
}

AssetBrowser::Dir& AssetBrowser::_cache_get(const std::filesystem::path& p_dir)
{
    auto [it, inserted] = dirs.try_emplace(p_dir);
    if (inserted) _cache_scan(p_dir, it->second);
    return it->second;
}

void AssetBrowser::_cache_scan(const std::filesystem::path& p_dir, Dir& r_dir)
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
        e.type = e.is_dir ? "Folder" : type_name(e.path.extension().string());
        e.lower_name = str_lowered(e.path.filename().string());
        e.is_texture = !e.is_dir && e.path.extension() == ".ltexture";
        e.icon = icon_for(e.path, e.is_dir);
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

void AssetBrowser::_tree_emit(const std::filesystem::path& p_dir, uint16_t p_depth)
{
    const Dir& node = _cache_get(p_dir);
    if (!node.exists) return;
    tree_rows.push_back({ &node, p_depth });
    if (p_depth != 0 && !open_folders.contains(p_dir)) return;
    for (const std::filesystem::path& sub : node.subdirs) _tree_emit(sub, (uint16_t)(p_depth + 1));
}

void AssetBrowser::_tree_toggle(const std::filesystem::path& p_dir)
{
    if (!open_folders.erase(p_dir)) open_folders.insert(p_dir);
}

void AssetBrowser::_draw_tree_row(const TreeRow& p_row, float p_row_h)
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
    _tree_row_hook(node);

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

void AssetBrowser::draw_tree(ImVec2 p_origin, float p_width, float p_height, bool p_vertical)
{
    const UiStyle& ui = ui_style();
    const UiStyle::Outliner& style = ui.outliner;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.padding.x, 0.0f));
    ImGui::BeginChild("##left", ImVec2(p_width, p_height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    const float row_h = ImMax(ImGui::GetFrameHeight(), ImGui::GetFontSize() + style.row_pad_y * 2.0f);
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.cell_pad_x, style.row_pad_y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, style.row_pad_y * 2.0f));
    if (ImGui::BeginTable("##folders", 1, flags)) {
        ImGui::TableSetupColumn("##folder", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder);

        tree_rows.clear();
        _tree_emit(root, 0);

        ImGuiListClipper clipper;
        clipper.Begin((int)tree_rows.size(), row_h);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) _draw_tree_row(tree_rows[i], row_h);
        }

        const ImVec2 corner(p_origin.x + p_width, p_origin.y + p_height);
        const float shadow_w = ui.asset_browser.divider_shadow;
        const ImU32 edge = ui.color.shadow;
        const ImU32 fade = ui.color.transparent;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(p_origin, corner, false);
        if (p_vertical) dl->AddRectFilledMultiColor(ImVec2(p_origin.x, corner.y - shadow_w), corner, fade, fade, edge, edge);
        else dl->AddRectFilledMultiColor(ImVec2(corner.x - shadow_w, p_origin.y), corner, fade, edge, edge, fade);
        dl->PopClipRect();
        ImGui::EndTable();
    }
    ImGui::PopStyleVar(2);
    ImGui::EndChild();
}

/*******************/
/**** SELECTION ****/
/*******************/

bool AssetBrowser::_is_selected(const std::filesystem::path& p_path) const
{
    for (const std::filesystem::path& p : selection) if (p == p_path) return true;
    return false;
}

void AssetBrowser::_select_only(const std::filesystem::path& p_path)
{
    selection.clear();
    selection.push_back(p_path);
    anchor = p_path;
}

void AssetBrowser::_clear_selection()
{
    selection.clear();
    anchor.clear();
}

void AssetBrowser::_click(const Dir& p_dir, uint32_t p_visible, bool p_ctrl, bool p_shift)
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

/***************/
/**** ITEMS ****/
/***************/

AssetInfo AssetBrowser::_resolve_asset(const std::filesystem::path& p_path)
{
    if (auto it = _thumb_infos.find(p_path); it != _thumb_infos.end()) return it->second;
    AssetInfo info = read_asset_info(p_path);
    if (!info.valid()) info = AssetInfo{};
    _thumb_infos.emplace(p_path, info);
    return info;
}

std::string AssetBrowser::_relative(const std::filesystem::path& p_path) const
{
    return p_path.lexically_relative(root).generic_string();
}

bool AssetBrowser::open_entry(EditorContext& ctx, const std::filesystem::path& p_path)
{
    const AssetInfo info = read_asset_info(p_path);
    if (!info.valid()) return false;
    switch (info.type) {
        case AssetType::MODEL: ctx.tabs->open<ModelTab>(info.guid)->path = p_path; break;
        case AssetType::MESH: ctx.tabs->open<MeshTab>(info.guid)->path = p_path; break;
        case AssetType::ANIMATION: ctx.tabs->open<AnimationTab>(info.guid)->path = p_path; break;
        case AssetType::BLEND_SPACE: ctx.tabs->open<BlendSpaceTab>(info.guid)->path = p_path; break;
        case AssetType::TERRAIN: ctx.tabs->open<TerrainTab>(info.guid)->path = p_path; break;
        case AssetType::OVERLAY: ctx.tabs->open<OverlayTab>(info.guid)->path = p_path; break;
        case AssetType::MATERIAL: ctx.tabs->open<MaterialTab>(info.guid)->path = p_path; break;
        case AssetType::TEXTURE: ctx.tabs->open<TextureTab>(info.guid)->path = p_path; break;
        case AssetType::WORLD:
            if (!ctx.world_tab) return false;
            ctx.world_tab->show(ctx, info.guid);
            break;
        default: return false;
    }
    return true;
}

void AssetBrowser::_drag_source(const Item& p_item)
{
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) return;
    if (!_is_selected(*p_item.path)) _select_only(*p_item.path);
    if (!ImGui::GetDragDropPayload()) drag_payload = AssetDragPayload::make(*p_item.path);
    ImGui::SetDragDropPayload(AssetDragPayload::TYPE, &drag_payload, sizeof(drag_payload), ImGuiCond_Once);
    if (selection.size() > 1) ImGui::Text(ICON_FA_LAYER_GROUP "  %u items", (uint32_t)selection.size());
    else ImGui::Text("%s  %s", p_item.icon ? p_item.icon : "", p_item.name);
    ImGui::EndDragDropSource();
}

void AssetBrowser::_draw_thumb(ImDrawList* p_dl, const Item& p_item, ImVec2 p_min, ImVec2 p_max, float p_icon_size) const
{
    if (p_item.texture) {
        p_dl->AddImage(p_item.texture, p_min, p_max);
        return;
    }
    if (!p_item.icon) return;
    const ImVec2 is = ImGui::GetFont()->CalcTextSizeA(p_icon_size, FLT_MAX, 0.0f, p_item.icon);
    const ImVec2 at(ImFloor(p_min.x + (p_max.x - p_min.x - is.x) * 0.5f), ImFloor(p_min.y + (p_max.y - p_min.y - is.y) * 0.5f));
    p_dl->AddText(ImGui::GetFont(), p_icon_size, at, p_item.icon_color, p_item.icon);
}

void AssetBrowser::_draw_progress(ImDrawList* p_dl, const Item& p_item, ImVec2 p_min, ImVec2 p_max) const
{
    if (p_item.progress < 0.0f || p_item.progress >= 1.0f) return;
    const float bar_h = ui_style().asset_browser.progress_height;
    const ImVec2 bar0(p_min.x, p_max.y - bar_h);
    p_dl->AddRectFilled(bar0, p_max, ImGui::GetColorU32(ImGuiCol_ModalWindowDimBg));
    const float w = (p_max.x - p_min.x) * p_item.progress;
    if (w > 0.5f) p_dl->AddRectFilled(bar0, ImVec2(bar0.x + w, p_max.y), ImGui::GetColorU32(ImGuiCol_Button));
}

void AssetBrowser::draw_items_column(EditorContext& ctx, ImVec2 p_size)
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.panel);
    ImGui::BeginChild("##right", p_size, ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    _draw_toolbar(ctx);
    const float footer_h = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    ImGui::BeginChild("##bottom_right", ImVec2(0, -footer_h), ImGuiChildFlags_None);
    draw_items(ctx);
    ImGui::EndChild();
    draw_footer();
    ImGui::EndChild();
}

void AssetBrowser::draw_items(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    const float margin = _item_margin();
    const float gap = _item_gap();
    const float avail_w = ImMax(1.0f, ImGui::GetContentRegionAvail().x - margin * 2.0f);
    const ImVec2 card = _item_size(avail_w);
    const float row_stride = card.y + gap;
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

        const bool importing = ctx.imports->busy();
        std::vector<std::filesystem::path> pending;
        if (importing) {
            for (const std::filesystem::path& ppath : ctx.imports->pending_out(selected_folder)) {
                if (query.empty() || str_lowered(ppath.filename().string()).find(query) != std::string::npos) pending.push_back(ppath);
            }
        }
        std::filesystem::path open_request;

        const uint32_t columns = _single_column() ? 1u : (uint32_t)ImMax(1.0f, ImFloor((avail_w + gap) / (card.x + gap)));
        const uint32_t total = (uint32_t)(_visible.size() + pending.size());
        const uint32_t row_count = (total + columns - 1) / columns;

        ImGui::Dummy(ImVec2(0.0f, gap));
        if (!_scroll_target.empty()) {
            for (uint32_t k = 0; k < (uint32_t)_visible.size(); ++k) {
                if (dir->entries[_visible[k]].path != _scroll_target) continue;
                const float y = (float)(k / columns) * row_stride;
                const float view_h = ImGui::GetWindowHeight();
                if (y < ImGui::GetScrollY() || y + row_stride > ImGui::GetScrollY() + view_h) ImGui::SetScrollY(y - (view_h - row_stride) * 0.5f);
                break;
            }
            _scroll_target.clear();
        }

        ImGuiListClipper clipper;
        clipper.Begin((int)row_count, row_stride);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const ImVec2 row_pos = ImGui::GetCursorScreenPos();
                for (uint32_t col = 0; col < columns; ++col) {
                    const uint32_t index = (uint32_t)row * columns + col;
                    if (index >= total) break;
                    ImGui::SetCursorScreenPos(ImVec2(row_pos.x + margin + (float)col * (card.x + gap), row_pos.y));

                    if (index >= (uint32_t)_visible.size()) {
                        const std::filesystem::path& ppath = pending[index - _visible.size()];
                        char ptype[32];
                        Item it;
                        it.progress = ctx.imports->progress(ppath);
                        std::snprintf(ptype, sizeof(ptype), "Importing %d%%", (int)(ImMax(it.progress, 0.0f) * 100.0f));
                        const std::string pname = (ppath.has_extension() && ppath.extension() != ".ltexture" ? ppath.filename() : ppath.stem()).string();
                        it.icon = ICON_FA_FILE_IMPORT;
                        it.icon_color = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                        it.name = pname.c_str();
                        it.subtitle = ptype;
                        it.path = &ppath;
                        it.importing = true;
                        ImGui::PushID((int)index);
                        _draw_item(ctx, it, card);
                        ImGui::PopID();
                        continue;
                    }

                    Entry& e = dir->entries[_visible[index]];
                    ImGui::PushID(e.lower_name.c_str());

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

                    const std::string subtitle = _item_subtitle(e);
                    Item it;
                    it.texture = (ImTextureID)set;
                    it.icon = e.icon;
                    it.icon_color = e.is_dir ? ui.color.folder : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    it.name = e.name.c_str();
                    it.subtitle = subtitle.c_str();
                    it.path = &e.path;
                    it.progress = importing ? ctx.imports->progress(e.path) : -1.0f;
                    const ItemResult r = _draw_item(ctx, it, card);
                    if (r.clicked) {
                        const ImGuiIO& io = ImGui::GetIO();
                        _click(*dir, index, io.KeyCtrl, io.KeyShift);
                    }
                    _item_hook(e);
                    if (r.activated && e.is_dir) open_request = e.path;
                    if (r.activated && !e.is_dir) open_entry(ctx, e.path);
                    ImGui::PopID();
                }
                ImGui::SetCursorScreenPos(row_pos);
                ImGui::Dummy(ImVec2(avail_w, row_stride));
            }
        }
        clipper.End();

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) _clear_selection();
        _items_end(ctx);
        if (!open_request.empty()) selected_folder = open_request;
    }
    ImGui::PopStyleVar();
}

void AssetBrowser::draw_footer()
{
    const uint32_t count = selected_folder.empty() ? 0u : (uint32_t)_cache_get(selected_folder).entries.size();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui_style().outliner.pad_x);
    ImGui::TextDisabled("%u items (%u selected)", count, (uint32_t)selection.size());
}

}
