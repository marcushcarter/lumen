#include <editor/tabs/world/panels/outliner_panel.h>
#include <editor/application/editor_context.h>
#include <editor/ui/tab_strip.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <editor/tabs/world/scene/editor_selection.h>
#include <editor/tabs/world/world_tab.h>
#include <editor/tabs/world/scene/editor_spawn.h>
#include <editor/tabs/prefab/prefab_tab.h>
#include <editor/assets/asset_drag_payload.h>
#include <core/world/world.h>
#include <core/world/prefab.h>
#include <core/world/components.h>
#include <drivers/windows/dialogs_win32.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace lumen {

static bool _is_digit(char c) { return c >= '0' && c <= '9'; }

static const char* _entity_icon(const World& world, Entity p_entity, bool p_open)
{
    if (world.is_folder(p_entity)) return p_open ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;
    if (world.has<PrefabInstanceComponent>(p_entity) && !world.has<PrefabPartComponent>(p_entity)) return ICON_FA_BOX;
    if (world.has<ModelGridComponent>(p_entity)) return ICON_FA_TABLE_CELLS;
    if (world.has<ModelComponent>(p_entity)) return ICON_FA_CUBES;
    return ICON_FA_CIRCLE_DOT;
}

static const char* _entity_name(const World& world, Entity p_entity)
{
    const NameComponent* n = world.try_get<NameComponent>(p_entity);
    return n && n->name[0] ? n->name : nullptr;
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

const OutlinerPanel::Node* OutlinerPanel::_node(Entity p_entity) const
{
    if (p_entity.index >= node_of.size() || node_of[p_entity.index] == NONE) return nullptr;
    const Node& n = nodes[node_of[p_entity.index]];
    return n.entity == p_entity ? &n : nullptr;
}

void OutlinerPanel::_refresh(const World& world)
{
    const bool world_changed = world_version != world.structure_version || entity_version != world.entity_version;
    if (!dirty && !world_changed) return;
    const double now = ImGui::GetTime();
    if (!dirty && !expect_world_change && last_rebuild >= 0.0 && now - last_rebuild < WORLD_REFRESH_INTERVAL) return;
    expect_world_change = false;
    last_rebuild = now;
    _rebuild(world);
}

void OutlinerPanel::_rebuild(const World& world)
{
    world_version = world.structure_version;
    entity_version = world.entity_version;
    dirty = false;

    const bool filtering = filter[0] != '\0';
    nodes.clear();
    char label[96];
    uint32_t max_index = 0;
    world.view<EntityIdComponent>([&](Entity e, const EntityIdComponent&) {
        Node n{};
        n.entity = e;
        n.parent = NONE;
        n.folder = world.is_folder(e);
        n.match = true;
        if (filtering) {
            _entity_label(_entity_name(world, e), e, label);
            n.match = ImStristr(label, nullptr, filter, nullptr) != nullptr;
        }
        nodes.push_back(n);
        max_index = std::max(max_index, e.index);
    });
    node_of.assign((size_t)max_index + 1, NONE);
    for (uint32_t i = 0; i < (uint32_t)nodes.size(); i++) node_of[nodes[i].entity.index] = i;
    for (Node& n : nodes) {
        const Entity p = world.parent(n.entity);
        if (const Node* pn = _node(p)) n.parent = (uint32_t)(pn - nodes.data());
    }

    order.resize(nodes.size());
    for (uint32_t i = 0; i < (uint32_t)nodes.size(); i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        const Node& na = nodes[a];
        const Node& nb = nodes[b];
        if (na.parent != nb.parent) return na.parent < nb.parent;
        if (na.folder != nb.folder) return na.folder;
        const char* la = _entity_name(world, na.entity);
        const char* lb = _entity_name(world, nb.entity);
        const int c = _natural_cmp(la ? la : "Entity", lb ? lb : "Entity");
        if (c) return sort_ascending ? c < 0 : c > 0;
        return na.entity.index < nb.entity.index;
    });
    roots.clear();
    for (uint32_t i = 0; i < (uint32_t)order.size(); i++) {
        Node& n = nodes[order[i]];
        if (n.parent == NONE) {
            roots.push_back(order[i]);
            continue;
        }
        Node& p = nodes[n.parent];
        if (p.child_count == 0) p.first_child = i;
        p.child_count++;
    }

    std::vector<uint32_t> walk(roots.begin(), roots.end());
    for (size_t i = 0; i < walk.size(); i++) {
        const Node& n = nodes[walk[i]];
        for (uint32_t c = 0; c < n.child_count; c++) walk.push_back(order[n.first_child + c]);
    }
    for (uint32_t i : walk) {
        Node& n = nodes[i];
        n.subtree = 1;
        n.subtree_hidden = world.has<EditorHiddenTag>(n.entity) ? 1u : 0u;
        n.matches = n.match ? 1u : 0u;
    }
    for (size_t i = walk.size(); i-- > 0;) {
        const Node& n = nodes[walk[i]];
        if (n.parent == NONE) continue;
        Node& p = nodes[n.parent];
        p.subtree += n.subtree;
        p.subtree_hidden += n.subtree_hidden;
        p.matches += n.matches;
    }

    rows.clear();
    for (uint32_t r : roots) _emit(world, r, 0);
}

void OutlinerPanel::_emit(const World& world, uint32_t p_node, uint16_t p_depth)
{
    const Node& n = nodes[p_node];
    const bool filtering = filter[0] != '\0';
    if (filtering && n.matches == 0) return;
    rows.push_back({ n.entity, p_depth, n.child_count > 0 });
    if (!filtering && !world.has<EditorExpandedTag>(n.entity)) return;
    for (uint32_t c = 0; c < n.child_count; c++) _emit(world, order[n.first_child + c], (uint16_t)(p_depth + 1));
}

void OutlinerPanel::draw_contents(EditorContext& ctx)
{
    if (!ctx.prefab_tab && ctx.world_tab && !ctx.world_tab->has_world()) {
        ImGui::TextDisabled("No world open");
        return;
    }
    if (!ctx.world || !ctx.selection) return;
    World& world = *ctx.world;
    EditorSelection& selection = *ctx.selection;

    if (rename_entity != ENTITY_NULL && !world.valid(rename_entity)) _rename_end();

    if (selection.primary() != last_primary) {
        last_primary = selection.primary();
        if (last_primary != ENTITY_NULL) _reveal(ctx, last_primary);
    }

    _refresh(world);

    _draw_toolbar(ctx);
    const float footer_h = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    _draw_table(ctx, footer_h);
    _draw_footer(ctx);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && world.valid(selection.primary())) _rename_begin(world, selection.primary());
        if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) _delete_selection(ctx);
    }
}

void OutlinerPanel::_draw_toolbar(EditorContext& ctx)
{
    const UiStyle& ui = ui_style();
    const float pad_x = ui.outliner.pad_x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);

    const char* label = ICON_FA_PLUS "  Add";
    const float bw = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    if (ui_search_bar("##outliner_search", filter, sizeof(filter), -(bw + spacing + pad_x))) dirty = true;
    ImGui::SameLine();
    ui_push_buttons(UiButton::PRIMARY);
    if (ImGui::Button(label, ImVec2(bw, 0.0f))) ImGui::OpenPopup("##outliner_add");
    ui_pop_buttons();

    const ImVec2 bmax = ImGui::GetItemRectMax();
    ImGui::SetNextWindowPos(ImVec2(bmax.x, bmax.y + ui.menu.popup_offset), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    ui_context_menu_push_style();
    if (ImGui::BeginPopup("##outliner_add")) {
        if (ImGui::MenuItem(ICON_FA_FOLDER "  Folder")) _add(ctx, SpawnType::FOLDER);
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_CIRCLE_DOT "  Empty")) _add(ctx, SpawnType::EMPTY);
        ImGui::EndPopup();
    }
    ui_context_menu_pop_style();
}

void OutlinerPanel::_draw_table(EditorContext& ctx, float p_footer_h)
{
    World& world = *ctx.world;
    const UiStyle::Outliner& style = ui_style().outliner;
    const float row_h = ImMax(ImGui::GetFrameHeight(), ImGui::GetFontSize() + style.row_pad_y * 2.0f);

    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(style.cell_pad_x, style.row_pad_y));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, style.row_pad_y * 2.0f));
    if (!ImGui::BeginTable("##outliner", 2, flags, ImVec2(0.0f, -p_footer_h))) {
        ImGui::PopStyleVar(2);
        return;
    }
    const bool scene_rows = !ctx.prefab_tab;
    ImGui::TableSetupScrollFreeze(0, !scene_rows ? 1 : world_expanded ? 3 : 2);
    ImGui::TableSetupColumn("##visibility", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder, row_h);
    ImGui::TableSetupColumn("Item Label", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder);

    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableSetColumnIndex(0);
    ImGui::TableHeader("##visibility");
    ui_text_centered(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Text), ICON_FA_EYE);
    ImGui::TableSetColumnIndex(1);
    ImGui::TableHeader(ImGui::TableGetColumnName(1));

    if (scene_rows) _draw_world_row(ctx, row_h);
    if (scene_rows && world_expanded) {
        if (_draw_settings_row(row_h, "environment", ICON_FA_CLOUD_SUN, "Environment", ctx.selection->environment)) {
            ctx.selection->select_environment();
            anchor = ENTITY_NULL;
            last_primary = ENTITY_NULL;
        }
    }

    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs()) {
        if (specs->SpecsDirty) {
            if (specs->SpecsCount > 0) sort_ascending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
            specs->SpecsDirty = false;
            _rebuild(world);
        }
    }

    if (scroll_to_selected) {
        scroll_to_selected = false;
        const int index = _row_find(ctx.selection->primary());
        if (index >= 0) {
            const float y = (float)index * row_h;
            const float view_h = ImGui::GetWindowHeight() - ImGui::TableGetHeaderRowHeight() - row_h;
            const float scroll = ImGui::GetScrollY();
            if (y < scroll || y + row_h > scroll + view_h) ImGui::SetScrollY(y - (view_h - row_h) * 0.5f);
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
        if (ImGui::AcceptDragDropPayload(PAYLOAD)) _drop(ctx, ENTITY_NULL);
        _asset_drop(ctx, ENTITY_NULL);
        ImGui::EndDragDropTarget();
    }
    ImGui::PopClipRect();
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered() && !io.KeyCtrl && !io.KeyShift) _clear_selection(ctx);
    ui_menu_push_style();
    if (ImGui::BeginPopupContextWindow("##outliner_empty", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu(ICON_FA_PLUS "  Add Entity")) {
            if (editor_spawn_menu_items(ctx, ENTITY_NULL) != ENTITY_NULL) expect_world_change = true;
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS "  New Folder")) {
            _clear_selection(ctx);
            _new_folder(ctx);
        }
        ImGui::EndPopup();
    }
    ui_menu_pop_style();

    ImGui::EndTable();
    ImGui::PopStyleVar(2);
}

void OutlinerPanel::_draw_row(EditorContext& ctx, int p_index, float p_row_h)
{
    World& world = *ctx.world;
    EditorSelection& selection = *ctx.selection;
    const Row row = rows[p_index];
    const Node* node = _node(row.entity);
    const bool folder = node && node->folder;
    const bool selected = selection.contains(row.entity);
    const bool self_hidden = world.has<EditorHiddenTag>(row.entity);
    const uint32_t descendants = node ? node->subtree - 1 : 0u;
    const uint32_t hidden_descendants = node ? node->subtree_hidden - (self_hidden ? 1u : 0u) : 0u;
    const bool hidden = self_hidden || (folder && descendants > 0 && hidden_descendants == descendants);
    const bool open = row.has_children && (filter[0] != '\0' || world.has<EditorExpandedTag>(row.entity));
    const UiStyle& ui = ui_style();
    const float font = ImGui::GetFontSize();
    const float content_h = p_row_h - ui.outliner.row_pad_y * 2.0f;

    char label[96];
    _entity_label(_entity_name(world, row.entity), row.entity, label);
    const char* icon = _entity_icon(world, row.entity, open);

    ImGui::TableNextRow(ImGuiTableRowFlags_None, p_row_h);
    ImGui::PushID((int)row.entity.index);

    ImGui::TableSetColumnIndex(1);
    const ImVec2 cell = ImGui::GetCursorScreenPos();
    const float text_y = cell.y + ImFloor((content_h - font) * 0.5f);
    const ImGuiSelectableFlags sel_flags = ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick;
    if (ImGui::Selectable("##row", selected, sel_flags, ImVec2(0.0f, content_h))) {
        const ImGuiIO& io = ImGui::GetIO();
        _click(ctx, p_index, io.KeyCtrl, io.KeyShift);
        if (row.has_children && !io.KeyCtrl && !io.KeyShift && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) _set_expanded(world, row.entity, !open);
    }
    const bool context = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseReleased(ImGuiMouseButton_Right));
    if (context && !selected) _select_only(ctx, p_index);
    if (rename_entity != row.entity && ImGui::BeginDragDropSource()) {
        if (!selection.contains(row.entity)) _select_only(ctx, p_index);
        const DragPayload payload{ selection.count() };
        ImGui::SetDragDropPayload(PAYLOAD, &payload, sizeof(payload));
        if (payload.count > 1) ImGui::Text(ICON_FA_LAYER_GROUP "  %u items", payload.count);
        else ImGui::Text("%s  %s", icon, label);
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload(PAYLOAD, ImGuiDragDropFlags_AcceptPeekOnly) && _can_drop(ctx, row.entity)) {
            if (ImGui::AcceptDragDropPayload(PAYLOAD)) _drop(ctx, row.entity);
        }
        _asset_drop(ctx, row.entity);
        ImGui::EndDragDropTarget();
    }
    ui_menu_push_style();
    if (ImGui::BeginPopupContextItem("##row_menu")) {
        _row_menu(ctx, row);
        ImGui::EndPopup();
    }
    ui_menu_pop_style();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool prefab_root = world.has<PrefabInstanceComponent>(row.entity) && !world.has<PrefabPartComponent>(row.entity);
    const bool prefab_part = world.has<PrefabPartComponent>(row.entity);
    ImU32 text_col = ImGui::GetColorU32(hidden ? ImGuiCol_TextDisabled : ImGuiCol_Text);
    if (prefab_root || prefab_part) text_col = ui_with_alpha(ui.color.asset_prefab, hidden ? ui.alpha.disabled : prefab_root ? 1.0f : 0.8f);
    float x = cell.x + (float)row.depth * font;
    if (row.has_children) {
        ImGui::SetCursorScreenPos(ImVec2(x, cell.y));
        if (ImGui::InvisibleButton("##arrow", ImVec2(font, content_h))) _set_expanded(world, row.entity, !open);
        const ImU32 arrow_col = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImGui::RenderArrow(dl, ImVec2(x, text_y + font * ui.outliner.arrow_offset_scale), arrow_col, open ? ImGuiDir_Down : ImGuiDir_Right, ui.outliner.arrow_scale);
    }
    x += font;

    const float icon_w = font * ui.outliner.icon_width_scale;
    const ImVec2 icon_size = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(ImFloor(x + (icon_w - icon_size.x) * 0.5f), text_y), folder ? ui.color.folder : text_col, icon);
    x += icon_w + ui.outliner.icon_gap;

    if (rename_entity == row.entity) {
        const UiRename rename = ui_inline_rename(ImVec2(x, text_y), -FLT_MIN, rename_buffer, sizeof(rename_buffer), rename_focus);
        if (rename != UiRename::NONE) {
            if (rename == UiRename::COMMIT) _rename_commit(ctx);
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
        else _set_hidden(world, row.entity, !hidden);
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

    ImGui::PopID();
}

void OutlinerPanel::_draw_world_row(EditorContext& ctx, float p_row_h)
{
    (void)ctx;
    const UiStyle& ui = ui_style();
    const float font = ImGui::GetFontSize();
    const float content_h = p_row_h - ui.outliner.row_pad_y * 2.0f;

    ImGui::TableNextRow(ImGuiTableRowFlags_None, p_row_h);
    ImGui::PushID("world_folder");
    ImGui::TableSetColumnIndex(1);
    const ImVec2 cell = ImGui::GetCursorScreenPos();
    const float text_y = cell.y + ImFloor((content_h - font) * 0.5f);
    ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0.0f, content_h));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) world_expanded = !world_expanded;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
    float x = cell.x;
    ImGui::SetCursorScreenPos(ImVec2(x, cell.y));
    if (ImGui::InvisibleButton("##arrow", ImVec2(font, content_h))) world_expanded = !world_expanded;
    const ImU32 arrow_col = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    ImGui::RenderArrow(dl, ImVec2(x, text_y + font * ui.outliner.arrow_offset_scale), arrow_col, world_expanded ? ImGuiDir_Down : ImGuiDir_Right, ui.outliner.arrow_scale);
    x += font;

    const char* icon = world_expanded ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;
    const float icon_w = font * ui.outliner.icon_width_scale;
    const ImVec2 icon_size = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(ImFloor(x + (icon_w - icon_size.x) * 0.5f), text_y), ui.color.folder, icon);
    x += icon_w + ui.outliner.icon_gap;
    dl->AddText(ImVec2(x, text_y), text_col, "World");
    ImGui::PopID();
}

bool OutlinerPanel::_draw_settings_row(float p_row_h, const char* p_id, const char* p_icon, const char* p_label, bool p_selected)
{
    const UiStyle& ui = ui_style();
    const float font = ImGui::GetFontSize();
    const float content_h = p_row_h - ui.outliner.row_pad_y * 2.0f;

    ImGui::TableNextRow(ImGuiTableRowFlags_None, p_row_h);
    ImGui::PushID(p_id);
    ImGui::TableSetColumnIndex(1);
    const ImVec2 cell = ImGui::GetCursorScreenPos();
    const float text_y = cell.y + ImFloor((content_h - font) * 0.5f);
    const bool clicked = ImGui::Selectable("##row", p_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap, ImVec2(0.0f, content_h));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
    float x = cell.x + font * 2.0f;
    const float icon_w = font * ui.outliner.icon_width_scale;
    const ImVec2 icon_size = ImGui::CalcTextSize(p_icon);
    dl->AddText(ImVec2(ImFloor(x + (icon_w - icon_size.x) * 0.5f), text_y), text_col, p_icon);
    x += icon_w + ui.outliner.icon_gap;
    dl->AddText(ImVec2(x, text_y), text_col, p_label);
    ImGui::PopID();
    return clicked;
}

void OutlinerPanel::_draw_footer(EditorContext& ctx)
{
    uint32_t entities = 0;
    for (const Node& n : nodes) entities += n.folder ? 0u : 1u;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ui_style().outliner.pad_x);
    ImGui::TextDisabled("%u entities (%u selected)", entities, ctx.selection->count());
}

void OutlinerPanel::_row_menu(EditorContext& ctx, const Row& p_row)
{
    World& world = *ctx.world;
    const uint32_t count = ctx.selection->count();
    if (ImGui::BeginMenu(ICON_FA_PLUS "  Add Child")) {
        if (editor_spawn_menu_items(ctx, p_row.entity) != ENTITY_NULL) expect_world_change = true;
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_PEN "  Rename", "F2")) _rename_begin(world, p_row.entity);
    if (ImGui::MenuItem(ICON_FA_FOLDER_PLUS "  Move To New Folder")) _new_folder(ctx);
    if (ImGui::MenuItem(ICON_FA_ARROW_UP "  Move To Root")) _drop(ctx, ENTITY_NULL);
    ImGui::Separator();
    if (!ctx.prefab_tab && ImGui::MenuItem(ICON_FA_BOX "  Create Prefab")) {
        if (editor_create_prefab(ctx, _top_selection(ctx))) expect_world_change = true;
    }
    if (const Prefab* prefab = prefab_for(world, p_row.entity)) {
        const Guid guid = prefab->guid;
        const bool part = prefab_is_part(world, p_row.entity);
        if (ImGui::BeginMenu(ICON_FA_BOX "  Prefab")) {
            if (ImGui::MenuItem(ICON_FA_UP_RIGHT_FROM_SQUARE "  Open Prefab")) editor_open_prefab(ctx, guid);
            if (part && ImGui::MenuItem(ICON_FA_ARROW_POINTER "  Select Instance")) ctx.selection->select(prefab_root_of(world, p_row.entity));
            ImGui::Separator();
            if (ImGui::MenuItem(part ? ICON_FA_ROTATE_LEFT "  Revert Entity" : ICON_FA_ROTATE_LEFT "  Revert Instance")) {
                editor_revert_prefab(ctx, p_row.entity);
                expect_world_change = true;
            }
            if (ImGui::MenuItem(ICON_FA_BOX_OPEN "  Unpack")) {
                editor_unpack_prefab(ctx, p_row.entity);
                expect_world_change = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Turns the instance into plain entities that no longer follow the prefab");
            ImGui::EndMenu();
        }
    }
    ImGui::Separator();
    char del[48];
    snprintf(del, sizeof(del), count > 1 ? ICON_FA_TRASH_CAN "  Delete %u Items" : ICON_FA_TRASH_CAN "  Delete", count);
    if (ImGui::MenuItem(del, "Del")) _delete_selection(ctx);
    bool any_children = false;
    for (const Entity e : ctx.selection->entities) {
        const Node* n = _node(e);
        any_children = any_children || (n && n->child_count > 0);
    }
    if (any_children) {
        if (ImGui::MenuItem(ICON_FA_FOLDER_MINUS "  Delete, Keep Children")) _delete_now(ctx, true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Removes the selected entities and moves their children up one level");
    }
}

int OutlinerPanel::_row_find(Entity p_entity) const
{
    for (int i = 0; i < (int)rows.size(); i++) {
        if (rows[i].entity == p_entity) return i;
    }
    return -1;
}

void OutlinerPanel::_click(EditorContext& ctx, int p_index, bool p_ctrl, bool p_shift)
{
    EditorSelection& selection = *ctx.selection;
    const Entity e = rows[p_index].entity;
    const int from = p_shift && anchor != ENTITY_NULL ? _row_find(anchor) : -1;
    if (from >= 0) {
        if (!p_ctrl) selection.clear();
        const int lo = ImMin(from, p_index);
        const int hi = ImMax(from, p_index);
        for (int i = lo; i <= hi; i++) selection.add(rows[i].entity);
        selection.add(e);
    } else if (p_ctrl) {
        selection.toggle(e);
        anchor = e;
    } else {
        _select_only(ctx, p_index);
    }
    last_primary = selection.primary();
}

void OutlinerPanel::_select_only(EditorContext& ctx, int p_index)
{
    ctx.selection->select(rows[p_index].entity);
    anchor = rows[p_index].entity;
    last_primary = ctx.selection->primary();
}

void OutlinerPanel::_clear_selection(EditorContext& ctx)
{
    ctx.selection->clear();
    anchor = ENTITY_NULL;
    last_primary = ENTITY_NULL;
}

void OutlinerPanel::_set_expanded(World& world, Entity p_entity, bool p_expanded)
{
    if (world.has<EditorExpandedTag>(p_entity) == p_expanded) return;
    if (p_expanded) world.deferred_add<EditorExpandedTag>(p_entity);
    else world.deferred_remove<EditorExpandedTag>(p_entity);
    expect_world_change = true;
}

void OutlinerPanel::_reveal(EditorContext& ctx, Entity p_entity)
{
    World& world = *ctx.world;
    uint32_t steps = 0;
    for (Entity e = world.parent(p_entity); e != ENTITY_NULL && steps < World::MAX_DEPTH; e = world.parent(e), steps++) _set_expanded(world, e, true);
    scroll_to_selected = true;
}

std::vector<Entity> OutlinerPanel::_top_selection(const EditorContext& ctx) const
{
    const World& world = *ctx.world;
    std::vector<Entity> out;
    for (const Entity e : ctx.selection->entities) {
        if (!world.valid(e)) continue;
        bool nested = false;
        for (const Entity o : ctx.selection->entities) nested = nested || (o != e && world.is_ancestor(o, e));
        if (!nested) out.push_back(e);
    }
    return out;
}

void OutlinerPanel::_new_folder(EditorContext& ctx)
{
    World& world = *ctx.world;
    const std::vector<Entity> moving = _top_selection(ctx);
    const Entity primary = ctx.selection->primary();
    const Entity parent = world.valid(primary) ? world.parent(primary) : ENTITY_NULL;
    const Entity folder = editor_spawn(ctx, SpawnType::FOLDER, parent);
    for (const Entity e : moving) world.set_parent(e, folder);
    if (!moving.empty()) world.add<EditorExpandedTag>(folder);
    _rename_begin(world, folder);
    expect_world_change = true;
}

void OutlinerPanel::_add(EditorContext& ctx, SpawnType p_type)
{
    World& world = *ctx.world;
    const Entity primary = ctx.selection->primary();
    Entity parent = ENTITY_NULL;
    if (world.valid(primary)) parent = world.is_folder(primary) ? primary : world.parent(primary);
    if (parent == ENTITY_NULL && ctx.prefab_tab) parent = world.find(ctx.prefab_tab->root);
    const Entity e = editor_spawn(ctx, p_type, parent);
    _rename_begin(world, e);
    scroll_to_selected = true;
    expect_world_change = true;
}

void OutlinerPanel::_delete_selection(EditorContext& ctx)
{
    const World& world = *ctx.world;
    const std::vector<Entity> top = _top_selection(ctx);
    uint32_t children = 0;
    for (const Entity e : top) {
        const Node* n = _node(e);
        children += n ? n->subtree - 1 : 0u;
    }
    if (children == 0) {
        _delete_now(ctx, false);
        return;
    }

    char label[96];
    _entity_label(_entity_name(world, top.front()), top.front(), label);
    const std::wstring counts = std::to_wstring(children) + (children == 1 ? L" child?" : L" children?");
    const std::wstring heading = top.size() == 1 ? L"Delete \"" + std::filesystem::path(reinterpret_cast<const char8_t*>(label)).wstring() + L"\" and its " + counts : L"Delete " + std::to_wstring(top.size()) + L" entities and their " + counts;
    if (drivers::Win32Dialogs::confirm(L"Delete", heading.c_str(), L"This can't be undone.")) _delete_now(ctx, false);
}

void OutlinerPanel::_delete_now(EditorContext& ctx, bool p_keep_children)
{
    World& world = *ctx.world;
    const std::vector<Entity> top = _top_selection(ctx);
    std::vector<Entity> list;
    for (const Entity e : top) {
        if (p_keep_children) {
            const Entity up = world.parent(e);
            world.children(e, list);
            for (const Entity c : list) world.set_parent(c, up);
            world.deferred_destroy(e);
            continue;
        }
        world.subtree(e, list);
        for (const Entity d : list) world.deferred_destroy(d);
    }
    expect_world_change = true;
    _clear_selection(ctx);
}

void OutlinerPanel::_asset_drop(EditorContext& ctx, Entity p_target)
{
    const ImGuiPayload* peek = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE, ImGuiDragDropFlags_AcceptPeekOnly);
    if (!peek) return;
    const AssetDragPayload& asset = *(const AssetDragPayload*)peek->Data;
    const bool prefab = asset.type == AssetType::PREFAB && !ctx.prefab_tab;
    if (!asset.guid.valid() || (!prefab && asset.type != AssetType::MODEL)) return;
    if (!ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) return;
    Entity parent = p_target;
    if (parent == ENTITY_NULL && ctx.prefab_tab) parent = ctx.world->find(ctx.prefab_tab->root);
    const Entity e = prefab ? editor_spawn_prefab(ctx, asset.guid, parent) : editor_spawn(ctx, SpawnType::MODEL, parent, asset.guid);
    if (e != ENTITY_NULL) {
        scroll_to_selected = true;
        expect_world_change = true;
    }
}

bool OutlinerPanel::_can_drop(const EditorContext& ctx, Entity p_target) const
{
    if (p_target == ENTITY_NULL) return true;
    const World& world = *ctx.world;
    for (const Entity e : ctx.selection->entities) {
        if (e == p_target || world.is_ancestor(e, p_target)) return false;
    }
    return true;
}

void OutlinerPanel::_drop(EditorContext& ctx, Entity p_target)
{
    World& world = *ctx.world;
    if (!_can_drop(ctx, p_target)) return;
    bool moved = false;
    for (const Entity e : _top_selection(ctx)) moved = world.set_parent(e, p_target) || moved;
    if (moved && world.valid(p_target)) _set_expanded(world, p_target, true);
    expect_world_change = true;
}

void OutlinerPanel::_set_hidden(World& world, Entity p_entity, bool p_hidden)
{
    std::vector<Entity> list;
    world.subtree(p_entity, list);
    for (const Entity e : list) {
        if (world.has<EditorHiddenTag>(e) == p_hidden) continue;
        if (p_hidden) world.deferred_add<EditorHiddenTag>(e);
        else world.deferred_remove<EditorHiddenTag>(e);
    }
}

void OutlinerPanel::_set_hidden_selection(EditorContext& ctx, bool p_hidden)
{
    World& world = *ctx.world;
    for (const Entity e : _top_selection(ctx)) _set_hidden(world, e, p_hidden);
    expect_world_change = true;
}

void OutlinerPanel::_rename_begin(const World& world, Entity p_entity)
{
    if (!world.valid(p_entity)) return;
    char label[96];
    _entity_label(_entity_name(world, p_entity), p_entity, label);
    snprintf(rename_buffer, sizeof(rename_buffer), "%s", label);
    rename_entity = p_entity;
    rename_focus = true;
}

void OutlinerPanel::_rename_commit(EditorContext& ctx)
{
    if (rename_buffer[0] == '\0') return;
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
    rename_entity = ENTITY_NULL;
    rename_focus = false;
}

}
