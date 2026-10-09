#include <editor/docking/panels/outliner_panel.h>
#include <editor/editor_context.h>
#include <editor/docking/tab_strip.h>
#include <editor/world/editor_folders.h>
#include <editor/world/editor_selection.h>
#include <editor/world/editor_spawn.h>
#include <core/world/world.h>
#include <core/world/components.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace lumen {

static constexpr float ROW_PAD_Y = 3.0f;

static bool _is_digit(char c) { return c >= '0' && c <= '9'; }

static const char* _entity_type(const World& world, Entity p_entity, const char** r_icon)
{
    if (world.has<MeshGridComponent>(p_entity)) { *r_icon = ICON_FA_TABLE_CELLS; return "Mesh Grid"; }
    if (world.has<MeshComponent>(p_entity)) { *r_icon = ICON_FA_CUBE; return world.has<StaticTag>(p_entity) ? "Static Mesh" : "Mesh"; }
    *r_icon = ICON_FA_CIRCLE_DOT;
    return "Entity";
}

static void _entity_label(const char* p_name, Entity p_entity, char (&r_label)[96])
{
    if (p_name) snprintf(r_label, sizeof(r_label), "%s", p_name);
    else snprintf(r_label, sizeof(r_label), "Entity %u", p_entity.index);
}

static int _natural_cmp(const char* a, const char* b)
{
    while (*a && *b) {
        if (_is_digit(*a) && _is_digit(*b)) {
            while (*a == '0') a++;
            while (*b == '0') b++;
            const char* ea = a;
            const char* eb = b;
            while (_is_digit(*ea)) ea++;
            while (_is_digit(*eb)) eb++;
            if (ea - a != eb - b) return (ea - a) < (eb - b) ? -1 : 1;
            for (; a < ea; a++, b++) {
                if (*a != *b) return *a < *b ? -1 : 1;
            }
            continue;
        }
        const int d = ImToUpper(*a) - ImToUpper(*b);
        if (d) return d;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int _label_cmp(const OutlinerPanel::Item& a, const OutlinerPanel::Item& b)
{
    const int c = _natural_cmp(a.name ? a.name : "Entity", b.name ? b.name : "Entity");
    if (c) return c;
    return a.entity.index < b.entity.index ? -1 : (a.entity.index > b.entity.index ? 1 : 0);
}

uint32_t OutlinerPanel::_slot(const EditorFolders& folders, uint32_t p_folder) const
{
    return folders.valid(p_folder) ? p_folder : (uint32_t)folders.folders.size();
}

void OutlinerPanel::_refresh(const World& world, const EditorFolders& folders)
{
    const bool user = dirty || folders_version != folders.version;
    const bool world_changed = world_version != world.structure_version;
    if (!user && !world_changed) return;

    const bool order = user || entity_version != world.entity_version || (sort_column == SortColumn::TYPE && world_changed);
    const double now = ImGui::GetTime();
    if (!user && !expect_world_change && last_rebuild >= 0.0 && now - last_rebuild < WORLD_REFRESH_INTERVAL) return;
    expect_world_change = false;
    last_rebuild = now;
    _rebuild(world, folders, order);
}

void OutlinerPanel::_rebuild(const World& world, const EditorFolders& folders, bool p_full)
{
    world_version = world.structure_version;
    entity_version = world.entity_version;
    folders_version = folders.version;
    dirty = false;

    const uint32_t folder_count = (uint32_t)folders.folders.size();
    const uint32_t slots = folder_count + 1;
    const uint32_t root = folder_count;
    const bool filtering = filter[0] != '\0';

    if (p_full) {
        items.clear();
        if (const std::vector<Entity>* entities = world.entities_with<EntityIdComponent>()) {
            items.reserve(entities->size());
            char label[96];
            for (const Entity e : *entities) {
                const NameComponent* n = world.try_get<NameComponent>(e);
                const char* icon;
                Item it;
                it.slot = _slot(folders, folders.entity_folder(world, e));
                it.entity = e;
                it.name = n && n->name[0] ? n->name : nullptr;
                it.type = _entity_type(world, e, &icon);
                it.match = true;
                if (filtering) {
                    _entity_label(it.name, e, label);
                    it.match = ImStristr(label, nullptr, filter, nullptr) != nullptr;
                }
                items.push_back(it);
            }
        }
        std::sort(items.begin(), items.end(), [&](const Item& a, const Item& b) {
            if (a.slot != b.slot) return a.slot < b.slot;
            int c = sort_column == SortColumn::TYPE ? std::strcmp(a.type, b.type) : 0;
            if (c == 0) c = _label_cmp(a, b);
            return sort_ascending ? c < 0 : c > 0;
        });
        for (Item& it : items) it.name = nullptr;
        item_begin.assign(slots + 1, 0);
        for (const Item& it : items) item_begin[it.slot + 1]++;
        for (uint32_t s = 0; s < slots; s++) item_begin[s + 1] += item_begin[s];

        folder_order.clear();
        for (uint32_t f = 0; f < folder_count; f++) {
            if (folders.folders[f].alive) folder_order.push_back(f);
        }
        const bool folders_descending = sort_column == SortColumn::LABEL && !sort_ascending;
        std::sort(folder_order.begin(), folder_order.end(), [&](uint32_t a, uint32_t b) {
            const uint32_t pa = _slot(folders, folders.folders[a].parent);
            const uint32_t pb = _slot(folders, folders.folders[b].parent);
            if (pa != pb) return pa < pb;
            const int c = _natural_cmp(folders.folders[a].name, folders.folders[b].name);
            if (c == 0) return a < b;
            return folders_descending ? c > 0 : c < 0;
        });
        folder_begin.assign(slots + 1, 0);
        for (const uint32_t f : folder_order) folder_begin[_slot(folders, folders.folders[f].parent) + 1]++;
        for (uint32_t s = 0; s < slots; s++) folder_begin[s + 1] += folder_begin[s];
    }

    folder_total.assign(slots, 0);
    folder_hidden.assign(slots, 0);
    folder_matches.assign(slots, 0);
    for (const Item& it : items) {
        const bool hidden = world.has<EditorHiddenTag>(it.entity);
        for (uint32_t s = it.slot; ; s = _slot(folders, folders.folders[s].parent)) {
            folder_total[s]++;
            folder_hidden[s] += hidden ? 1u : 0u;
            folder_matches[s] += it.match ? 1u : 0u;
            if (s == root) break;
        }
    }
    if (filtering) {
        for (const uint32_t f : folder_order) {
            if (!ImStristr(folders.folders[f].name, nullptr, filter, nullptr)) continue;
            for (uint32_t s = f; ; s = _slot(folders, folders.folders[s].parent)) {
                folder_matches[s]++;
                if (s == root) break;
            }
        }
    }

    rows.clear();
    _emit(folders, root, 0);
}

void OutlinerPanel::_emit(const EditorFolders& folders, uint32_t p_slot, uint16_t p_depth)
{
    const bool filtering = filter[0] != '\0';
    const uint32_t root = (uint32_t)folders.folders.size();
    for (uint32_t i = folder_begin[p_slot]; i < folder_begin[p_slot + 1]; i++) {
        const uint32_t f = folder_order[i];
        if (filtering && folder_matches[f] == 0) continue;
        rows.push_back({ ENTITY_NULL, f, p_depth, RowKind::FOLDER });
        if (filtering || folders.folders[f].expanded) _emit(folders, f, (uint16_t)(p_depth + 1));
    }
    const uint32_t folder = p_slot == root ? EditorFolders::ROOT : p_slot;
    for (uint32_t i = item_begin[p_slot]; i < item_begin[p_slot + 1]; i++) {
        if (items[i].match) rows.push_back({ items[i].entity, folder, p_depth, RowKind::ENTITY });
    }
}

void OutlinerPanel::draw_contents(EditorContext& ctx)
{
    if (!ctx.world || !ctx.selection || !ctx.folders) return;
    World& world = *ctx.world;
    EditorFolders& folders = *ctx.folders;
    EditorSelection& selection = *ctx.selection;

    if (rename_kind == RowKind::FOLDER && rename_folder != NONE && !folders.valid(rename_folder)) _rename_end();
    if (rename_kind == RowKind::ENTITY && rename_entity != ENTITY_NULL && !world.valid(rename_entity)) _rename_end();

    if (selection.primary() != last_primary) {
        last_primary = selection.primary();
        if (last_primary != ENTITY_NULL) _reveal(ctx, last_primary);
    }

    _refresh(world, folders);

    _draw_toolbar(ctx);
    const float footer_h = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    _draw_table(ctx, footer_h);
    _draw_footer(ctx);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            if (world.valid(selection.primary())) _rename_begin_entity(world, selection.primary());
            else if (folders.valid(selection.primary_folder())) _rename_begin_folder(folders, selection.primary_folder());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) _delete_selection(ctx);
    }
}

void OutlinerPanel::_draw_toolbar(EditorContext& ctx)
{
    const float bw = ImGui::GetFrameHeight();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + PAD_X);
    if (dock_search_bar("##outliner_search", filter, sizeof(filter), -(bw + ImGui::GetStyle().ItemSpacing.x + PAD_X))) dirty = true;
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_PLUS, ImVec2(bw, bw))) _new_folder(ctx);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Create folder (moves the selection into it)");
}

void OutlinerPanel::_draw_table(EditorContext& ctx, float p_footer_h)
{
    World& world = *ctx.world;
    EditorFolders& folders = *ctx.folders;
    const float row_h = ImMax(ImGui::GetFrameHeight(), ImGui::GetFontSize() + ROW_PAD_Y * 2.0f);

    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, ROW_PAD_Y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, ROW_PAD_Y * 2.0f));
    if (!ImGui::BeginTable("##outliner", 3, flags, ImVec2(0.0f, -p_footer_h))) {
        ImGui::PopStyleVar(2);
        return;
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##visibility", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder, row_h);
    ImGui::TableSetupColumn("Item Label", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder, 0.6f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoReorder, 0.4f);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableSetColumnIndex(0);
    ImGui::TableHeader("##visibility");
    {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec2 ts = ImGui::CalcTextSize(ICON_FA_EYE);
        ImGui::GetWindowDrawList()->AddText(ImVec2(ImFloor((min.x + max.x - ts.x) * 0.5f), ImFloor((min.y + max.y - ts.y) * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), ICON_FA_EYE);
    }
    for (int c = 1; c < 3; c++) {
        ImGui::TableSetColumnIndex(c);
        ImGui::TableHeader(ImGui::TableGetColumnName(c));
    }

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if (specs->SpecsDirty) {
            if (specs->SpecsCount > 0) {
                sort_column = specs->Specs[0].ColumnIndex == 2 ? SortColumn::TYPE : SortColumn::LABEL;
                sort_ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
            }
            specs->SpecsDirty = false;
            _rebuild(world, folders, true);
        }
    }

    if (scroll_to_selected) {
        scroll_to_selected = false;
        for (uint32_t i = 0; i < (uint32_t)rows.size(); i++) {
            if (rows[i].kind != RowKind::ENTITY || rows[i].entity != ctx.selection->primary()) continue;
            const float y = (float)i * row_h;
            const float view_h = ImGui::GetWindowHeight() - ImGui::TableGetHeaderRowHeight();
            const float scroll = ImGui::GetScrollY();
            if (y < scroll || y + row_h > scroll + view_h) ImGui::SetScrollY(y - (view_h - row_h) * 0.5f);
            break;
        }
    }

    ImGuiListClipper clipper;
    clipper.Begin((int)rows.size(), row_h);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) _draw_row(ctx, i, row_h);
    }

    const ImRect inner = ImGui::GetCurrentWindow()->Rect();
    ImGui::PushClipRect(inner.Min, inner.Max, false);
    if (ImGui::BeginDragDropTargetCustom(inner, ImGui::GetID("##root_drop"))) {
        if (ImGui::AcceptDragDropPayload(PAYLOAD)) _drop(ctx, EditorFolders::ROOT);
        ImGui::EndDragDropTarget();
    }
    ImGui::PopClipRect();
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && !io.KeyCtrl && !io.KeyShift) _clear_selection(ctx);
    dock_menu_push_style();
    if (ImGui::BeginPopupContextWindow("##outliner_empty", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu(ICON_FA_PLUS "  Add Entity")) {
            editor_spawn_menu_items(ctx, EditorFolders::ROOT);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS "  New Folder")) {
            _clear_selection(ctx);
            _new_folder(ctx);
        }
        ImGui::EndPopup();
    }
    dock_menu_pop_style();

    ImGui::EndTable();
    ImGui::PopStyleVar(2);
}

void OutlinerPanel::_draw_row(EditorContext& ctx, int p_index, float p_row_h)
{
    World& world = *ctx.world;
    EditorFolders& folders = *ctx.folders;
    EditorSelection& selection = *ctx.selection;
    const Row p_row = rows[p_index];
    const bool is_folder = p_row.kind == RowKind::FOLDER;
    const bool selected = _row_selected(selection, p_row);
    const bool hidden = is_folder ? folder_total[p_row.folder] > 0 && folder_hidden[p_row.folder] == folder_total[p_row.folder] : world.has<EditorHiddenTag>(p_row.entity);
    const bool v_open = is_folder && (filter[0] != '\0' || folders.folders[p_row.folder].expanded);
    const float font = ImGui::GetFontSize();
    const float content_h = p_row_h - ROW_PAD_Y * 2.0f;

    char label[96];
    const char* icon;
    const char* type;
    if (is_folder) {
        snprintf(label, sizeof(label), "%s", folders.folders[p_row.folder].name);
        icon = v_open ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;
        type = "Folder";
    } else {
        const NameComponent* n = world.try_get<NameComponent>(p_row.entity);
        _entity_label(n && n->name[0] ? n->name : nullptr, p_row.entity, label);
        type = _entity_type(world, p_row.entity, &icon);
    }

    ImGui::TableNextRow(ImGuiTableRowFlags_None, p_row_h);
    ImGui::PushID(is_folder ? "f" : "e");
    ImGui::PushID((int)(is_folder ? p_row.folder : p_row.entity.index));

    ImGui::TableSetColumnIndex(1);
    const ImVec2 cell = ImGui::GetCursorScreenPos();
    const float text_y = cell.y + ImFloor((content_h - font) * 0.5f);
    const ImGuiSelectableFlags sel_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick;
    // pressed on click-release, so pressing on an already selected row and dragging keeps the multi-selection
    if (ImGui::Selectable("##row", selected, sel_flags, ImVec2(0.0f, content_h))) {
        const ImGuiIO& io = ImGui::GetIO();
        _click(ctx, p_index, io.KeyCtrl, io.KeyShift);
        if (is_folder && !io.KeyCtrl && !io.KeyShift && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            folders.folders[p_row.folder].expanded = !folders.folders[p_row.folder].expanded;
            dirty = true;
        }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !selected) _select_only(ctx, p_index);
    if (!_renaming(p_row) && ImGui::BeginDragDropSource()) {
        if (!_row_selected(selection, p_row)) _select_only(ctx, p_index);
        const DragPayload payload{ selection.count() };
        ImGui::SetDragDropPayload(PAYLOAD, &payload, sizeof(payload));
        if (payload.count > 1) ImGui::Text(ICON_FA_LAYER_GROUP "  %u items", payload.count);
        else ImGui::Text("%s  %s", icon, label);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload(PAYLOAD, ImGuiDragDropFlags_AcceptPeekOnly) && _can_drop(ctx, p_row.folder)) {
            if (ImGui::AcceptDragDropPayload(PAYLOAD)) _drop(ctx, p_row.folder);
        }
        ImGui::EndDragDropTarget();
    }
    dock_menu_push_style();
    if (ImGui::BeginPopupContextItem("##row_menu")) {
        _row_menu(ctx, p_row);
        ImGui::EndPopup();
    }
    dock_menu_pop_style();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 text_col = ImGui::GetColorU32(hidden ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    float x = cell.x + (float)p_row.depth * font;
    if (is_folder) {
        ImGui::SetCursorScreenPos(ImVec2(x, cell.y));
        if (ImGui::InvisibleButton("##arrow", ImVec2(font, content_h))) {
            folders.folders[p_row.folder].expanded = !folders.folders[p_row.folder].expanded;
            dirty = true;
        }
        const ImU32 arrow_col = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImGui::RenderArrow(dl, ImVec2(x, text_y + font * 0.15f), arrow_col, v_open ? ImGuiDir_Down : ImGuiDir_Right, 0.7f);
    }
    x += font;

    const float icon_w = font * 1.25f;
    const ImVec2 icon_size = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(ImFloor(x + (icon_w - icon_size.x) * 0.5f), text_y), is_folder ? IM_COL32(144, 100, 41, 255) : text_col, icon);
    x += icon_w + 4.0f;

    if (_renaming(p_row)) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
        ImGui::SetCursorScreenPos(ImVec2(x - 2.0f, text_y));
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (rename_focus) {
            ImGui::SetKeyboardFocusHere();
            rename_focus = false;
        }
        const bool enter = ImGui::InputText("##rename", rename_buffer, sizeof(rename_buffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::PopStyleVar();
        if (enter || ImGui::IsItemDeactivated()) {
            if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false)) _rename_commit(ctx);
            _rename_end();
        }
    } else {
        dl->AddText(ImVec2(x, text_y), text_col, label);
    }

    ImGui::TableSetColumnIndex(0);
    const ImVec2 eye_pos = ImGui::GetCursorScreenPos();
    const float eye_w = ImMax(1.0f, ImGui::GetContentRegionAvail().x);
    if (ImGui::InvisibleButton("##eye", ImVec2(eye_w, content_h))) {
        if (selected) _set_hidden_selection(ctx, !hidden);
        else if (is_folder) _set_hidden(world, folders, p_row.folder, !hidden);
        else if (hidden) world.deferred_remove<EditorHiddenTag>(p_row.entity);
        else world.deferred_add<EditorHiddenTag>(p_row.entity);
        expect_world_change = true;
    }
    const bool eye_hovered = ImGui::IsItemHovered();
    if (eye_hovered) ImGui::SetTooltip(hidden ? "Show in editor" : "Hide in editor");
    const bool row_hovered = ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex();
    if (hidden || selected || row_hovered || eye_hovered) {
        const char* eye = hidden ? ICON_FA_EYE_SLASH : ICON_FA_EYE;
        const ImVec2 ts = ImGui::CalcTextSize(eye);
        ImGui::GetWindowDrawList()->AddText(ImVec2(ImFloor(eye_pos.x + (eye_w - ts.x) * 0.5f), text_y), ImGui::GetColorU32(eye_hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled), eye);
    }

    ImGui::TableSetColumnIndex(2);
    ImGui::GetWindowDrawList()->AddText(ImVec2(ImGui::GetCursorScreenPos().x, text_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), type);

    ImGui::PopID();
    ImGui::PopID();
}

void OutlinerPanel::_draw_footer(EditorContext& ctx)
{
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + PAD_X);
    ImGui::TextDisabled("%u entities (%u selected)", (uint32_t)items.size(), ctx.selection->count());
}

void OutlinerPanel::_row_menu(EditorContext& ctx, const Row& p_row)
{
    World& world = *ctx.world;
    EditorFolders& folders = *ctx.folders;
    const uint32_t count = ctx.selection->count();
    if (ImGui::BeginMenu(ICON_FA_PLUS "  Add Entity")) {
        editor_spawn_menu_items(ctx, p_row.folder);
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (p_row.kind == RowKind::FOLDER) {
        if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS "  New Subfolder")) {
            const uint32_t f = folders.create("New Folder", p_row.folder);
            folders.folders[p_row.folder].expanded = true;
            _rename_begin_folder(folders, f);
        }
        if (ImGui::MenuItem(ICON_FA_PEN "  Rename", "F2")) _rename_begin_folder(folders, p_row.folder);
    } else {
        if (ImGui::MenuItem(ICON_FA_PEN "  Rename", "F2")) _rename_begin_entity(world, p_row.entity);
    }
    if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS "  Move To New Folder")) _new_folder(ctx);
    if (ImGui::MenuItem(ICON_FA_ARROW_UP "  Move To Root")) _drop(ctx, EditorFolders::ROOT);
    ImGui::Separator();
    char del[48];
    snprintf(del, sizeof(del), count > 1 ? ICON_FA_TRASH_CAN "  Delete %u Items" : ICON_FA_TRASH_CAN "  Delete", count);
    if (ImGui::MenuItem(del, "Del")) _delete_selection(ctx);
    if (!ctx.selection->folders.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Deleted folders move their contents to the parent folder");
}

bool OutlinerPanel::_row_selected(const EditorSelection& selection, const Row& p_row) const
{
    return p_row.kind == RowKind::FOLDER ? selection.contains_folder(p_row.folder) : selection.contains(p_row.entity);
}

void OutlinerPanel::_row_add(EditorSelection& selection, const Row& p_row) const
{
    if (p_row.kind == RowKind::FOLDER) selection.add_folder(p_row.folder);
    else selection.add(p_row.entity);
}

int OutlinerPanel::_row_find(const Row& p_row) const
{
    for (int i = 0; i < (int)rows.size(); i++) {
        const Row& r = rows[i];
        if (r.kind != p_row.kind) continue;
        if (r.kind == RowKind::FOLDER ? r.folder == p_row.folder : r.entity == p_row.entity) return i;
    }
    return -1;
}

void OutlinerPanel::_click(EditorContext& ctx, int p_index, bool p_ctrl, bool p_shift)
{
    EditorSelection& selection = *ctx.selection;
    const Row row = rows[p_index];
    const int from = p_shift && has_anchor ? _row_find(anchor) : -1;
    if (from >= 0) {
        if (!p_ctrl) selection.clear();
        const int lo = ImMin(from, p_index);
        const int hi = ImMax(from, p_index);
        for (int i = lo; i <= hi; i++) _row_add(selection, rows[i]);
        _row_add(selection, row);
    } else if (p_ctrl) {
        if (row.kind == RowKind::FOLDER) selection.toggle_folder(row.folder);
        else selection.toggle(row.entity);
        anchor = row;
        has_anchor = true;
    } else {
        _select_only(ctx, p_index);
    }
    last_primary = selection.primary();
}

void OutlinerPanel::_select_only(EditorContext& ctx, int p_index)
{
    ctx.selection->clear();
    _row_add(*ctx.selection, rows[p_index]);
    anchor = rows[p_index];
    has_anchor = true;
    last_primary = ctx.selection->primary();
}

void OutlinerPanel::_clear_selection(EditorContext& ctx)
{
    ctx.selection->clear();
    has_anchor = false;
    last_primary = ENTITY_NULL;
}

void OutlinerPanel::_reveal(EditorContext& ctx, Entity p_entity)
{
    EditorFolders& folders = *ctx.folders;
    for (uint32_t f = folders.entity_folder(*ctx.world, p_entity); f != EditorFolders::ROOT && folders.valid(f); f = folders.folders[f].parent) {
        folders.folders[f].expanded = true;
    }
    dirty = true;
    scroll_to_selected = true;
}

void OutlinerPanel::_new_folder(EditorContext& ctx)
{
    World& world = *ctx.world;
    EditorFolders& folders = *ctx.folders;
    const EditorSelection& selection = *ctx.selection;
    uint32_t parent = EditorFolders::ROOT;
    if (world.valid(selection.primary())) parent = folders.entity_folder(world, selection.primary());
    else if (folders.valid(selection.primary_folder())) parent = folders.folders[selection.primary_folder()].parent;
    const uint32_t f = folders.create("New Folder", parent);
    folders.expand_to(parent);
    if (!selection.empty()) _drop(ctx, f);
    _rename_begin_folder(folders, f);
    dirty = true;
}

void OutlinerPanel::_delete_selection(EditorContext& ctx)
{
    EditorSelection& selection = *ctx.selection;
    for (const uint32_t f : selection.folders) ctx.folders->destroy(*ctx.world, f);
    for (const Entity e : selection.entities) ctx.world->deferred_destroy(e);
    if (!selection.entities.empty()) expect_world_change = true;
    _clear_selection(ctx);
}

bool OutlinerPanel::_can_drop(const EditorContext& ctx, uint32_t p_target) const
{
    if (p_target == EditorFolders::ROOT) return true;
    for (const uint32_t f : ctx.selection->folders) {
        if (ctx.folders->is_ancestor(f, p_target)) return false;
    }
    return true;
}

void OutlinerPanel::_drop(EditorContext& ctx, uint32_t p_target)
{
    World& world = *ctx.world;
    EditorFolders& folders = *ctx.folders;
    const EditorSelection& selection = *ctx.selection;
    if (!_can_drop(ctx, p_target)) return;

    auto under_selected = [&](uint32_t p_folder) {
        for (uint32_t f = p_folder; f != EditorFolders::ROOT && folders.valid(f); f = folders.folders[f].parent) {
            if (selection.contains_folder(f)) return true;
        }
        return false;
    };
    std::vector<uint32_t> move_folders;
    std::vector<Entity> move_entities;
    for (const uint32_t f : selection.folders) {
        if (folders.valid(f) && !under_selected(folders.folders[f].parent)) move_folders.push_back(f);
    }
    for (const Entity e : selection.entities) {
        if (world.valid(e) && !under_selected(folders.entity_folder(world, e))) move_entities.push_back(e);
    }
    for (const uint32_t f : move_folders) folders.set_parent(f, p_target);
    for (const Entity e : move_entities) folders.entity_set_folder(world, e, p_target);
    if (folders.valid(p_target)) {
        folders.folders[p_target].expanded = true;
        dirty = true;
    }
}

void OutlinerPanel::_set_hidden_selection(EditorContext& ctx, bool p_hidden)
{
    World& world = *ctx.world;
    for (const uint32_t f : ctx.selection->folders) {
        if (ctx.folders->valid(f)) _set_hidden(world, *ctx.folders, f, p_hidden);
    }
    for (const Entity e : ctx.selection->entities) {
        if (!world.valid(e) || world.has<EditorHiddenTag>(e) == p_hidden) continue;
        if (p_hidden) world.deferred_add<EditorHiddenTag>(e);
        else world.deferred_remove<EditorHiddenTag>(e);
    }
    expect_world_change = true;
}

void OutlinerPanel::_set_hidden(World& world, const EditorFolders& folders, uint32_t p_slot, bool p_hidden)
{
    for (uint32_t i = item_begin[p_slot]; i < item_begin[p_slot + 1]; i++) {
        const Entity e = items[i].entity;
        if (world.has<EditorHiddenTag>(e) == p_hidden) continue;
        if (p_hidden) world.deferred_add<EditorHiddenTag>(e);
        else world.deferred_remove<EditorHiddenTag>(e);
    }
    for (uint32_t i = folder_begin[p_slot]; i < folder_begin[p_slot + 1]; i++) _set_hidden(world, folders, folder_order[i], p_hidden);
}

void OutlinerPanel::_rename_begin_entity(const World& world, Entity p_entity)
{
    const NameComponent* n = world.try_get<NameComponent>(p_entity);
    char label[96];
    _entity_label(n && n->name[0] ? n->name : nullptr, p_entity, label);
    snprintf(rename_buffer, sizeof(rename_buffer), "%s", label);
    rename_kind = RowKind::ENTITY;
    rename_entity = p_entity;
    rename_folder = NONE;
    rename_focus = true;
}

void OutlinerPanel::_rename_begin_folder(const EditorFolders& folders, uint32_t p_folder)
{
    if (!folders.valid(p_folder)) return;
    snprintf(rename_buffer, sizeof(rename_buffer), "%s", folders.folders[p_folder].name);
    rename_kind = RowKind::FOLDER;
    rename_folder = p_folder;
    rename_entity = ENTITY_NULL;
    rename_focus = true;
}

void OutlinerPanel::_rename_commit(EditorContext& ctx)
{
    if (rename_buffer[0] == '\0') return;
    if (rename_kind == RowKind::FOLDER) {
        ctx.folders->rename(rename_folder, rename_buffer);
        return;
    }
    World& world = *ctx.world;
    if (!world.valid(rename_entity)) return;
    if (NameComponent* n = world.try_get<NameComponent>(rename_entity)) {
        snprintf(n->name, NameComponent::MAX_LENGTH, "%s", rename_buffer);
    } else {
        NameComponent name{};
        snprintf(name.name, NameComponent::MAX_LENGTH, "%s", rename_buffer);
        world.deferred_add<NameComponent>(rename_entity, name);
        expect_world_change = true;
    }
    dirty = true;
}

void OutlinerPanel::_rename_end()
{
    rename_folder = NONE;
    rename_entity = ENTITY_NULL;
    rename_focus = false;
}

bool OutlinerPanel::_renaming(const Row& p_row) const
{
    if (p_row.kind == RowKind::FOLDER) return rename_kind == RowKind::FOLDER && rename_folder == p_row.folder;
    return rename_kind == RowKind::ENTITY && rename_entity == p_row.entity;
}

}
