#include <editor/assets/content_browser_panel.h>
#include <core/assets/lmaterial.h>
#include <core/assets/lterrain.h>
#include <core/base/jobs.h>
#include <core/assets/lscene.h>
#include <core/base/strings.h>
#include <core/scene/scene_file.h>
#include <core/scene/scene.h>
#include <core/scene/template.h>
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
#include <editor/assets/thumbnail_cache.h>
#include <editor/assets/asset_field.h>
#include <editor/documents/scene/scene_document.h>
#include <editor/documents/document_manager.h>
#include <editor/documents/animation/animation_document.h>
#include <editor/documents/material/material_document.h>
#include <editor/documents/mesh/mesh_document.h>
#include <editor/documents/terrain/terrain_document.h>
#include <editor/documents/texture/texture_document.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <toml++/toml.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <sstream>

namespace lumen {

struct NewAssetDesc {
    const char* label;
    const char* stem;
    const char* ext;
};

static const NewAssetDesc NEW_ASSETS[] = {
    { "Scene", "New Scene", ".lscene" },
    { "Template", "New Template", ".ltemplate" },
    { "Terrain", "New Terrain", ".lterrain" },
    { "Material", "New Material", ".lmaterial" },
};
static_assert(std::size(NEW_ASSETS) == (size_t)ContentBrowserPanel::NewAsset::COUNT);

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
    return ext == ".lmaterial" || ext == ".lscene" || ext == ".ltemplate";
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

const char* ContentBrowserPanel::icon_for(const std::filesystem::path& p_path, bool p_is_dir)
{
    if (p_is_dir) return ICON_FA_FOLDER;
    const std::filesystem::path ext = p_path.extension();
    if (ext == ".ltexture") return TextureDocument::ICON;
    if (ext == ".lmesh") return MeshDocument::ICON;
    if (ext == ".lmaterial") return MaterialDocument::ICON;
    if (ext == ".lanim") return AnimationDocument::ICON;
    if (ext == ".lscene") return ICON_FA_EARTH_AMERICAS;
    if (ext == ".lterrain") return TerrainDocument::ICON;
    if (ext == ".ltemplate") return ICON_FA_BOX;
    return ICON_FA_FILE;
}

std::string ContentBrowserPanel::type_name(std::string p_ext)
{
    str_lower(p_ext);
    if (p_ext == ".lmesh") return "Mesh";
    if (p_ext == ".ltexture") return "Texture";
    if (p_ext == ".lmaterial") return "Material";
    if (p_ext == ".lanim") return "Animation";
    if (p_ext == ".lscene") return "Scene";
    if (p_ext == ".lterrain") return "Terrain";
    if (p_ext == ".ltemplate") return "Template";
    if (!p_ext.empty() && p_ext[0] == '.') p_ext.erase(0, 1);
    for (char& c : p_ext) c = (char)std::toupper((unsigned char)c);
    return p_ext;
}

bool ContentBrowserPanel::open_entry(EditorContext& ctx, const std::filesystem::path& p_path)
{
    const AssetInfo info = read_asset_info(p_path);
    if (!info.valid()) return false;
    switch (info.type) {
        case AssetType::MESH: ctx.documents->open<MeshDocument>(info.guid)->path = p_path; break;
        case AssetType::ANIMATION: ctx.documents->open<AnimationDocument>(info.guid)->path = p_path; break;
        case AssetType::TERRAIN: ctx.documents->open<TerrainDocument>(info.guid)->path = p_path; break;
        case AssetType::MATERIAL: ctx.documents->open<MaterialDocument>(info.guid)->path = p_path; break;
        case AssetType::TEXTURE: ctx.documents->open<TextureDocument>(info.guid)->path = p_path; break;
        case AssetType::TEMPLATE:
            if (!ctx.scene_document) return false;
            ctx.scene_document->open_template(ctx, info.guid, p_path);
            ctx.scene_document->focus_request = true;
            break;
        case AssetType::SCENE:
            if (!ctx.scene_document) return false;
            ctx.scene_document->open(info.guid);
            ctx.scene_document->focus_request = true;
            break;
        default: return false;
    }
    return true;
}

/***************/
/**** CACHE ****/
/***************/

void ContentBrowserPanel::_reset(const std::filesystem::path& p_root)
{
    root = p_root;
    dirs.clear();
    open_folders.clear();
    results.clear();
    _result_query.clear();
    _clear_selection();
    rename_target.clear();
    cancel_request.clear();
    delete_requested = false;
    last_refresh = -1.0e9;
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
    std::error_code ec;
    r_dir.exists = std::filesystem::is_directory(p_dir, ec);
    if (!r_dir.exists) return;
    for (auto it = std::filesystem::directory_iterator(p_dir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        Entry e;
        e.path = it->path();
        e.is_dir = it->is_directory(ec);
        e.name = e.path.filename().string();
        e.lower_name = str_lowered(e.name);
        e.icon = icon_for(e.path, e.is_dir);
        r_dir.entries.push_back(std::move(e));
    }
    std::sort(r_dir.entries.begin(), r_dir.entries.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;
        return a.lower_name < b.lower_name;
    });
}

void ContentBrowserPanel::_search_collect(const std::filesystem::path& p_dir, const std::string& p_query)
{
    const Dir& dir = _cache_get(p_dir);
    for (const Entry& e : dir.entries) {
        if (results.size() >= MAX_RESULTS) return;
        if (e.lower_name.find(p_query) != std::string::npos) results.push_back(e);
        if (e.is_dir) _search_collect(e.path, p_query);
    }
}

void ContentBrowserPanel::_toggle(const std::filesystem::path& p_dir)
{
    if (!open_folders.erase(p_dir)) open_folders.insert(p_dir);
}

void ContentBrowserPanel::_rows_emit(EditorContext& ctx, const std::filesystem::path& p_dir, uint16_t p_depth, uint64_t p_lines)
{
    const Dir& dir = _cache_get(p_dir);
    std::vector<std::filesystem::path> pending;
    if (ctx.imports && ctx.imports->busy()) pending = ctx.imports->pending_out(p_dir);
    const size_t total = dir.entries.size() + pending.size();
    size_t i = 0;
    for (const Entry& e : dir.entries) {
        const bool last = ++i == total;
        rows.push_back({ &e, {}, p_depth, last, p_lines });
        if (e.is_dir && open_folders.contains(e.path)) _rows_emit(ctx, e.path, (uint16_t)(p_depth + 1), last || p_depth > 64 ? p_lines : p_lines | (1ull << (p_depth - 1)));
    }
    for (const std::filesystem::path& p : pending) {
        const bool last = ++i == total;
        rows.push_back({ nullptr, p, p_depth, last, p_lines });
    }
}

/*******************/
/**** SELECTION ****/
/*******************/

bool ContentBrowserPanel::_is_selected(const std::filesystem::path& p_path) const
{
    return std::find(selection.begin(), selection.end(), p_path) != selection.end();
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

void ContentBrowserPanel::_click(const std::filesystem::path& p_path, bool p_ctrl, bool p_shift)
{
    const auto at = [&](const std::filesystem::path& p) { return (int)(std::find(_order.begin(), _order.end(), p) - _order.begin()); };
    const int to = at(p_path);
    const int from = p_shift && !anchor.empty() ? at(anchor) : (int)_order.size();
    if (from < (int)_order.size() && to < (int)_order.size()) {
        if (!p_ctrl) selection.clear();
        for (int k = ImMin(from, to); k <= ImMax(from, to); ++k) {
            if (!_is_selected(_order[k])) selection.push_back(_order[k]);
        }
        return;
    }
    if (p_ctrl) {
        auto it = std::find(selection.begin(), selection.end(), p_path);
        if (it != selection.end()) selection.erase(it);
        else selection.push_back(p_path);
        anchor = p_path;
        return;
    }
    _select_only(p_path);
}

/**************/
/**** DRAW ****/
/**************/

void ContentBrowserPanel::_drag_source(const Entry& p_entry)
{
    if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) return;
    if (!_is_selected(p_entry.path)) _select_only(p_entry.path);
    if (!ImGui::GetDragDropPayload()) drag_payload = AssetDragPayload::make(p_entry.path);
    ImGui::SetDragDropPayload(AssetDragPayload::TYPE, &drag_payload, sizeof(drag_payload), ImGuiCond_Once);
    if (selection.size() > 1) ImGui::Text("%u items", (uint32_t)selection.size());
    else ImGui::Text("%s  %s", p_entry.icon ? p_entry.icon : "", p_entry.name.c_str());
    ImGui::EndDragDropSource();
}

bool ContentBrowserPanel::_draw_row(EditorContext& ctx, const Entry& p_entry, uint16_t p_depth, float p_row_h, bool p_show_path, uint64_t p_lines, bool p_last)
{
    const UiStyle& ui = ui_style();
    const float font = ImGui::GetFontSize();
    const bool selected = _is_selected(p_entry.path);
    const bool renaming = rename_target == p_entry.path;
    const bool open = p_entry.is_dir && open_folders.contains(p_entry.path);
    _order.push_back(p_entry.path);

    ImGui::PushID(p_entry.path.string().c_str());
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p1(p0.x + w, p0.y + p_row_h);
    ImGui::SetNextItemAllowOverlap();
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(ImMax(1.0f, w), p_row_h));
    const bool hovered = ImGui::IsItemHovered();
    const bool context = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseReleased(ImGuiMouseButton_Right));
    if (context && !selected) _select_only(p_entry.path);
    if (clicked && !renaming) {
        const ImGuiIO& io = ImGui::GetIO();
        _click(p_entry.path, io.KeyCtrl, io.KeyShift);
    }
    const bool activated = hovered && !renaming && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (!p_entry.is_dir && !renaming && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::GetDragDropPayload()) {
        if (_hover_path != p_entry.path) {
            _hover_path = p_entry.path;
            _hover_info = read_asset_info(p_entry.path);
        }
        if (_hover_info.valid()) asset_tooltip(ctx, _hover_info.guid, _hover_info.type);
    }

    ui_context_menu_push_style();
    if (!renaming && ImGui::BeginPopupContextItem("##row_menu")) {
        _context_menu(ctx, p_entry.path);
        ImGui::EndPopup();
    }
    ui_context_menu_pop_style();
    if (!renaming) _drag_source(p_entry);
    if (p_entry.is_dir) _drop_target(p_entry.path);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_Header));
    else if (hovered) dl->AddRectFilled(p0, p1, ui.color.list_hover);

    const float text_y = ImFloor(p0.y + (p_row_h - font) * 0.5f);
    float x = p0.x + ui.outliner.pad_x + (float)p_depth * font;
    const bool has_box = p_entry.is_dir && !p_show_path;
    if (!p_show_path) ui_tree_lines(dl, p0.x + ui.outliner.pad_x, font, p0.y, p1.y, p_depth, p_lines, p_last, has_box ? x + ImFloor(font * 0.5f) - ui_tree_box_half() : x + font + ui.outliner.box_gap);
    if (has_box) {
        ImGui::SetCursorScreenPos(ImVec2(x, p0.y));
        if (ImGui::InvisibleButton("##arrow", ImVec2(font, p_row_h))) _toggle(p_entry.path);
        ui_tree_box(dl, ImVec2(x + font * 0.5f, p0.y + p_row_h * 0.5f), open, ImGui::IsItemHovered());
    }
    if (!p_show_path) x += font + ui.outliner.box_gap;

    const float icon_w = font * ui.outliner.icon_width_scale;
    const float icon_px = ImFloor(font * 1.2f);
    const ImVec2 icon_at(x + ImFloor((icon_w - icon_px) * 0.5f), p0.y + ImFloor((p_row_h - icon_px) * 0.5f));
    if (p_entry.is_dir) ui_icon_folder(dl, icon_at, icon_px);
    else ui_icon_file(dl, icon_at, icon_px);
    x += icon_w + ui.outliner.icon_gap;

    if (renaming) {
        const UiRename rename = ui_inline_rename(ImVec2(x, p0.y), ImMax(1.0f, p1.x - x - ui.space.sm), rename_buf, sizeof(rename_buf), rename_focus);
        if (rename != UiRename::NONE) {
            if (rename == UiRename::COMMIT) _rename_commit();
            rename_target.clear();
        }
    } else {
        dl->PushClipRect(ImVec2(x, p0.y), p1, true);
        if (p_show_path) {
            const std::string rel = p_entry.path.lexically_relative(root).generic_string();
            dl->AddText(ImVec2(x, text_y), ui.color.text, rel.c_str());
            const size_t name_at = rel.size() >= p_entry.name.size() ? rel.size() - p_entry.name.size() : 0;
            const size_t n = _result_query.size();
            if (n > 0) {
                for (size_t at = p_entry.lower_name.find(_result_query); at != std::string::npos; at = p_entry.lower_name.find(_result_query, at + n)) {
                    const char* s0 = rel.c_str() + name_at + at;
                    const float hx0 = x + ImGui::CalcTextSize(rel.c_str(), s0).x;
                    const float hx1 = hx0 + ImGui::CalcTextSize(s0, s0 + n).x;
                    dl->AddRectFilled(ImVec2(hx0, p0.y + 1.0f), ImVec2(hx1, p1.y - 1.0f), ui.color.search_match);
                    dl->AddText(ImVec2(hx0, text_y), IM_COL32(0, 0, 0, 255), s0, s0 + n);
                }
            }
        } else {
            dl->AddText(ImVec2(x, text_y), ui.color.text, p_entry.name.c_str());
        }
        if (ctx.imports && ctx.imports->busy() && !p_entry.is_dir) {
            const float progress = ctx.imports->progress(p_entry.path);
            if (progress >= 0.0f && progress < 1.0f) dl->AddRectFilled(ImVec2(p0.x, p1.y - ui.asset_browser.progress_height), ImVec2(p0.x + w * progress, p1.y), ui.color.accent);
        }
        dl->PopClipRect();
    }

    if (_scroll_target == p_entry.path) {
        ImGui::SetScrollHereY(0.5f);
        _scroll_target.clear();
    }
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
    ImGui::PopID();
    return activated;
}

void ContentBrowserPanel::_draw_pending(EditorContext& ctx, const std::filesystem::path& p_path, uint16_t p_depth, float p_row_h, uint64_t p_lines, bool p_last)
{
    const UiStyle& ui = ui_style();
    const float font = ImGui::GetFontSize();
    ImGui::PushID(p_path.string().c_str());
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##pending", ImVec2(ImMax(1.0f, w), p_row_h));
    ui_context_menu_push_style();
    if (ImGui::BeginPopupContextItem("##pending_menu")) {
        if (ImGui::MenuItem("Cancel Import")) cancel_request = p_path;
        ImGui::EndPopup();
    }
    ui_context_menu_pop_style();
    const float progress = ImMax(ctx.imports->progress(p_path), 0.0f);
    char label[320];
    std::snprintf(label, sizeof(label), "%s  (%d%%)", p_path.filename().string().c_str(), (int)(progress * 100.0f));
    const float x = p0.x + ui.outliner.pad_x + (float)p_depth * font + font + ui.outliner.box_gap + font * ui.outliner.icon_width_scale + ui.outliner.icon_gap;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float lx = p0.x + ui.outliner.pad_x + (float)p_depth * font;
    ui_tree_lines(dl, p0.x + ui.outliner.pad_x, font, p0.y, p0.y + p_row_h, p_depth, p_lines, p_last, lx + font + ui.outliner.box_gap);
    const float icon_w = font * ui.outliner.icon_width_scale;
    const float icon_px = ImFloor(font * 1.2f);
    ui_icon_file(dl, ImVec2(lx + font + ui.outliner.box_gap + ImFloor((icon_w - icon_px) * 0.5f), p0.y + ImFloor((p_row_h - icon_px) * 0.5f)), icon_px);
    dl->AddText(ImVec2(x, ImFloor(p0.y + (p_row_h - font) * 0.5f)), ui.color.text_dim, label);
    dl->AddRectFilled(ImVec2(p0.x, p0.y + p_row_h - ui.asset_browser.progress_height), ImVec2(p0.x + w * progress, p0.y + p_row_h), ui.color.accent);
    ImGui::PopID();
}

void ContentBrowserPanel::_draw_root(float p_row_h)
{
    const UiStyle& ui = ui_style();
    const float font = ImGui::GetFontSize();
    ImGui::PushID("##asset_root");
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p1(p0.x + w, p0.y + p_row_h);
    ImGui::SetNextItemAllowOverlap();
    if (ImGui::InvisibleButton("##row", ImVec2(ImMax(1.0f, w), p_row_h))) {
        _clear_selection();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) root_open = !root_open;
    }
    const bool hovered = ImGui::IsItemHovered();
    _drop_target(root);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered) dl->AddRectFilled(p0, p1, ui.color.list_hover);
    const float x = p0.x + ui.outliner.pad_x;
    ImGui::SetCursorScreenPos(ImVec2(x, p0.y));
    if (ImGui::InvisibleButton("##arrow", ImVec2(font, p_row_h))) root_open = !root_open;
    ui_tree_box(dl, ImVec2(x + ImFloor(font * 0.5f), p0.y + ImFloor(p_row_h * 0.5f)), root_open, ImGui::IsItemHovered());
    const float icon_w = font * ui.outliner.icon_width_scale;
    const float icon_px = ImFloor(font * 1.2f);
    ui_icon_folder(dl, ImVec2(x + font + ui.outliner.box_gap + ImFloor((icon_w - icon_px) * 0.5f), p0.y + ImFloor((p_row_h - icon_px) * 0.5f)), icon_px);
    const std::string name = root.filename().string();
    dl->AddText(ImVec2(x + font + ui.outliner.box_gap + icon_w + ui.outliner.icon_gap, ImFloor(p0.y + (p_row_h - font) * 0.5f)), ui.color.text, name.c_str());
    ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y));
    ImGui::PopID();
}

void ContentBrowserPanel::_list_end(EditorContext& ctx)
{
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift) _clear_selection();
    ui_context_menu_push_style();
    if (ImGui::BeginPopupContextWindow("##browser_empty", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        _common_assets_menu(ctx);
        ImGui::EndPopup();
    }
    ui_context_menu_pop_style();
}

void ContentBrowserPanel::_draw_list(EditorContext& ctx, const char* p_id, ImVec2 p_size, bool p_results)
{
    const UiStyle& ui = ui_style();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui.color.outliner_back);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::BeginChild(p_id, p_size, ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    if (visible) {
        const float row_h = ImGui::GetFontSize() + ui.outliner.row_pad_y * 2.0f;
        std::filesystem::path activate;
        if (p_results) {
            ImGuiListClipper clipper;
            clipper.Begin((int)results.size(), row_h);
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    if (_draw_row(ctx, results[i], 0, row_h, true)) activate = results[i].path;
                }
            }
        } else {
            rows.clear();
            _draw_root(row_h);
            if (root_open) _rows_emit(ctx, root, 1, 0);
            ImGuiListClipper clipper;
            clipper.Begin((int)rows.size(), row_h);
            if (!_scroll_target.empty()) {
                for (int i = 0; i < (int)rows.size(); i++) {
                    if (rows[i].entry && rows[i].entry->path == _scroll_target) clipper.IncludeItemByIndex(i);
                }
            }
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    const Row& r = rows[i];
                    if (!r.entry) {
                        _draw_pending(ctx, r.pending, r.depth, row_h, r.lines, r.last);
                        continue;
                    }
                    if (_draw_row(ctx, *r.entry, r.depth, row_h, false, r.lines, r.last)) activate = r.entry->path;
                }
            }
        }
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        if (!p_results) {
            const ImRect area = ImGui::GetCurrentWindow()->Rect();
            if (ImGui::BeginDragDropTargetCustom(area, ImGui::GetID("##root_drop"))) {
                _drop_accept(root);
                ImGui::EndDragDropTarget();
            }
        }
        _list_end(ctx);
        if (!activate.empty()) {
            std::error_code ec;
            if (std::filesystem::is_directory(activate, ec)) {
                if (p_results) {
                    for (std::filesystem::path p = activate; !p.empty() && p != root && p.has_parent_path(); p = p.parent_path()) open_folders.insert(p);
                    _select_only(activate);
                    _scroll_target = activate;
                } else {
                    _toggle(activate);
                }
            } else {
                open_entry(ctx, activate);
            }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void ContentBrowserPanel::draw_contents(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    registry = ctx.assets;
    if (root != ctx.project->assets_dir) _reset(ctx.project->assets_dir);
    _cache_tick(ImGui::GetTime());
    _order.clear();

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float bw = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + ImFloor((ui.row - bw) * 0.5f)));
    ImGui::SetNextItemWidth(ImMax(1.0f, ImGui::GetContentRegionAvail().x - bw - 1.0f));
    ImGui::InputTextWithHint("##asset_search", ICON_FA_MAGNIFYING_GLASS "  Search", search_buf, sizeof(search_buf));
    ImGui::SameLine(0.0f, 1.0f);
    if (ImGui::Button(ICON_FA_PLUS "##asset_new", ImVec2(bw, bw))) ImGui::OpenPopup("##asset_new_menu");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("New asset or folder");
    ui_context_menu_push_style();
    if (ImGui::BeginPopup("##asset_new_menu")) {
        _common_assets_menu(ctx);
        ImGui::EndPopup();
    }
    ui_context_menu_pop_style();

    const float list_y = origin.y + ui.row;
    const ImVec2 avail(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x - origin.x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - list_y);
    const std::string query = str_lowered(search_buf);
    ImGui::SetCursorScreenPos(ImVec2(origin.x, list_y));

    if (query.empty()) {
        results.clear();
        _result_query.clear();
        _draw_list(ctx, "##tree", ImVec2(avail.x, ImMax(1.0f, avail.y)), false);
    } else {
        if (query != _result_query || _result_time != last_refresh) {
            _result_query = query;
            _result_time = last_refresh;
            results.clear();
            _search_collect(root, query);
        }
        const float status_h = ImGui::GetTextLineHeightWithSpacing();
        const float body_h = ImMax(1.0f, avail.y - status_h);
        const float top_h = ImFloor(body_h * 0.5f);
        _draw_list(ctx, "##results", ImVec2(avail.x, top_h), true);
        ImGui::SetCursorScreenPos(ImVec2(origin.x + ui.space.sm, list_y + top_h));
        ImGui::TextDisabled("Done. %u item(s) found.%s", (uint32_t)results.size(), results.size() >= MAX_RESULTS ? " (limit reached)" : "");
        ImGui::SetCursorScreenPos(ImVec2(origin.x, list_y + top_h + status_h));
        _draw_list(ctx, "##tree", ImVec2(avail.x, ImMax(1.0f, body_h - top_h)), false);
    }

    if (delete_requested) {
        delete_requested = false;
        _delete_now(ctx);
    }
    if (_delete_refresh && _deleting->load() == 0u) {
        _delete_refresh = false;
        request_refresh();
    }
    if (!cancel_request.empty()) {
        ctx.imports->cancel(cancel_request);
        cancel_request.clear();
    }
}

/*****************/
/**** EDITING ****/
/*****************/

std::filesystem::path ContentBrowserPanel::_target_folder() const
{
    if (selection.empty()) return root;
    std::error_code ec;
    const std::filesystem::path& last = selection.back();
    return std::filesystem::is_directory(last, ec) ? last : last.parent_path();
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
    for (std::filesystem::path p = p_path.parent_path(); !p.empty() && p != root && p.has_parent_path(); p = p.parent_path()) open_folders.insert(p);
    _select_only(p_path);
    _rename_begin(p_path);
    _scroll_target = p_path;
    request_refresh();
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
    request_refresh();
}

void ContentBrowserPanel::_new_folder(EditorContext& ctx)
{
    (void)ctx;
    std::error_code ec;
    const std::filesystem::path path = _unique_path(_target_folder(), "New Folder", "");
    if (!std::filesystem::create_directory(path, ec)) return;
    _begin_new(path);
}

void ContentBrowserPanel::_duplicate_selection(EditorContext& ctx)
{
    std::vector<std::filesystem::path> created;
    for (const std::filesystem::path& p : selection) {
        if (!_duplicable(p)) continue;
        const std::filesystem::path dst = _duplicate_asset(p);
        if (dst.empty()) {
            log_write("Asset Browser: failed to duplicate '%s'.", p.string().c_str());
            continue;
        }
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

void ContentBrowserPanel::_import(EditorContext& ctx)
{
    const std::filesystem::path folder = _target_folder();
    std::error_code ec;
    if (!std::filesystem::exists(folder, ec)) return;
    std::vector<std::wstring> files = drivers::Win32Dialogs::open_files(
        L"All Supported\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.fbx;*.gltf;*.glb;*.obj;*.dae;*.stl;*.ply;*.3ds;*.blend\0"
        L"Models\0*.fbx;*.gltf;*.glb;*.obj;*.dae;*.stl;*.ply;*.3ds;*.blend\0"
        L"Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0"
        L"All Files\0*.*\0"
    );
    for (const std::wstring& f : files) {
        const std::filesystem::path source = f;
        if (!ctx.import_dialog->push(ctx, source, folder)) log_write("Skipped unsupported import: %s", source.string().c_str());
    }
    if (!files.empty() && folder != root) open_folders.insert(folder);
}

void ContentBrowserPanel::_move_selection(const std::filesystem::path& p_dst)
{
    for (const std::filesystem::path& p : selection) {
        if (_nested_in_selection(p) || p == p_dst || Paths::is_under(p_dst, p)) continue;
        Paths::move(p, p_dst);
    }
    _clear_selection();
    request_refresh();
}

void ContentBrowserPanel::_drop_target(const std::filesystem::path& p_dst)
{
    if (!ImGui::BeginDragDropTarget()) return;
    _drop_accept(p_dst);
    ImGui::EndDragDropTarget();
}

void ContentBrowserPanel::_drop_accept(const std::filesystem::path& p_dst)
{
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
        const AssetDragPayload* asset = (const AssetDragPayload*)payload->Data;
        if (asset->path[0]) {
            if (_is_selected(asset->path)) _move_selection(p_dst);
            else Paths::move(asset->path, p_dst);
        }
        request_refresh();
    }
}

void ContentBrowserPanel::_delete_content(EditorContext& ctx, const std::filesystem::path& p_asset, std::vector<std::filesystem::path>& r_files)
{
    AssetInfo info = read_asset_info(p_asset);
    if (!info.valid()) return;
    if (ctx.renderer) {
        switch (info.type) {
            case AssetType::TEXTURE: ctx.renderer->textures.unload(info.guid); break;
            case AssetType::MESH: ctx.renderer->geometry.unload(info.guid); ctx.renderer->skeletons.unload(info.guid); ctx.renderer->mesh_slots.unload(info.guid); break;
            case AssetType::MATERIAL: ctx.renderer->materials.unload(info.guid); break;
            default: break;
        }
    }
    r_files.push_back(ctx.project->content_path(info.guid));
}

void ContentBrowserPanel::_delete_files(std::vector<std::filesystem::path> p_files)
{
    if (p_files.empty()) return;
    _deleting->fetch_add(1u);
    _delete_refresh = true;
    job_system().run_background_owned([files = std::move(p_files), counter = _deleting]() {
        for (const std::filesystem::path& f : files) {
            Paths::remove_to_recycle(f);
            std::error_code ec;
            const std::filesystem::path parent = f.parent_path();
            if (f.extension() == ".bin" && std::filesystem::is_directory(parent, ec) && std::filesystem::is_empty(parent, ec) && !ec) std::filesystem::remove(parent, ec);
        }
        counter->fetch_sub(1u);
    }, nullptr, "Delete Assets");
}

void ContentBrowserPanel::_delete_asset(EditorContext& ctx, const std::filesystem::path& p_path)
{
    std::vector<std::filesystem::path> files;
    _delete_content(ctx, p_path, files);
    files.push_back(p_path);
    _delete_files(std::move(files));
}

void ContentBrowserPanel::_delete_folder(EditorContext& ctx, const std::filesystem::path& p_folder)
{
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::recursive_directory_iterator(p_folder, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_directory(ec)) continue;
        const std::filesystem::path ext = it->path().extension();
        if (ext == ".ltexture" || ext == ".lmesh" || ext == ".lmaterial" || ext == ".lanim" || ext == ".lterrain" || ext == ".lwater") _delete_content(ctx, it->path(), files);
    }
    files.push_back(p_folder);
    _delete_files(std::move(files));
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
        else log_write("Asset Browser: '%s' kept, some items already exist in the parent folder.", p.string().c_str());
    }
    _clear_selection();
    request_refresh();
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
    request_refresh();
}

void ContentBrowserPanel::_context_menu(EditorContext& ctx, const std::filesystem::path& p_row)
{
    std::error_code ec;
    const uint32_t count = (uint32_t)selection.size();
    bool any_folder = false;
    for (const std::filesystem::path& p : selection) any_folder = any_folder || std::filesystem::is_directory(p, ec);

    if (!any_folder && count == 1 && ImGui::MenuItem("Open")) open_entry(ctx, p_row);
    if (ImGui::MenuItem("Rename")) _rename_begin(p_row);
    if (_duplicable(p_row) && ImGui::MenuItem("Duplicate")) _duplicate_selection(ctx);
    if (!any_folder) {
        const bool reimportable = std::any_of(selection.begin(), selection.end(), [](const std::filesystem::path& p) { return can_reimport(p); });
        if (reimportable) {
            ImGui::Separator();
            const bool has_settings = std::any_of(selection.begin(), selection.end(), [](const std::filesystem::path& p) { return reimport_has_settings(reimport_target(p).kind); });
            if (ImGui::MenuItem("Reimport")) reimport_assets(ctx, selection);
            if (ImGui::MenuItem("Reimport With Settings...", nullptr, false, has_settings)) reimport_with_settings(ctx, selection);
        }
    }
    const AssetInfo info = count == 1 && !any_folder ? read_asset_info(p_row) : AssetInfo{};
    if (info.valid() && ctx.references_dialog) {
        ImGui::Separator();
        if (ImGui::MenuItem("Find References")) ctx.references_dialog->open(ctx, info.guid, ReferencesDialog::Mode::FIND);
        if (ImGui::MenuItem("Replace References...")) ctx.references_dialog->open(ctx, info.guid, ReferencesDialog::Mode::REPLACE);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Show in Explorer")) Paths::reveal_in_explorer(p_row);
    char del[48];
    std::snprintf(del, sizeof(del), count > 1 ? "Delete %u Items" : "Delete", count);
    if (ImGui::MenuItem(del)) _delete_selection();
    if (any_folder) {
        if (ImGui::MenuItem("Delete Folder Only")) _delete_folders_only();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Removes the folder and moves its contents to the parent folder");
    }
}

void ContentBrowserPanel::_create_asset(EditorContext& ctx, NewAsset p_kind)
{
    const NewAssetDesc& d = NEW_ASSETS[(size_t)p_kind];
    const std::filesystem::path path = _unique_path(_target_folder(), d.stem, d.ext);
    Error err = Error::FAILED;
    switch (p_kind) {
        case NewAsset::SCENE: { LScene a; a.guid = Guid::generate(); err = a.save(path); } break;
        case NewAsset::TEMPLATE: {
            Scene tmp;
            tmp.initialize();
            SceneFile file;
            file.guid = Guid::generate();
            file.root = Guid::generate();
            file.type = AssetType::TEMPLATE;
            const Entity template_root = tmp.create_persistent(file.root);
            NameComponent name{};
            std::snprintf(name.name, NameComponent::MAX_LENGTH, "%s", path.stem().string().c_str());
            tmp.add<NameComponent>(template_root, name);
            tmp.add<TransformComponent>(template_root);
            err = SceneFile::write(path, tmp, file);
        } break;
        case NewAsset::TERRAIN: { LTerrain a; a.guid = Guid::generate(); err = a.save(path); } break;
        case NewAsset::MATERIAL: { LMaterial a; a.guid = Guid::generate(); err = a.save(path); } break;
        case NewAsset::COUNT: break;
    }
    if (err != Error::OK) {
        log_write("Asset Browser: failed to create '%s'.", path.string().c_str());
        return;
    }
    if (ctx.renderer && p_kind == NewAsset::MATERIAL) ctx.renderer->materials.load(path);
    _begin_new(path);
}

void ContentBrowserPanel::_common_assets_menu(EditorContext& ctx)
{
    if (ImGui::MenuItem("New Folder")) _new_folder(ctx);
    if (ImGui::MenuItem("Import...")) _import(ctx);
    ImGui::Separator();
    for (size_t i = 0; i < (size_t)NewAsset::COUNT; ++i) {
        if (ImGui::MenuItem(NEW_ASSETS[i].label)) _create_asset(ctx, (NewAsset)i);
    }
}

}
