#include <editor/panels/content_browser_panel.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <editor/assets/import/reimport.h>
#include <editor/assets/references_dialog.h>
#include <core/assets/asset_common.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>

namespace lumen {

void ContentBrowserPanel::draw_contents(EditorContext& ctx)
{
    prepare(ctx);

    const UiStyle& ui = ui_style();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float thick = ui.asset_browser.splitter;
    const float flip = ui.asset_browser.layout_flip_ratio;
    if (!vertical && avail.y > avail.x * flip) vertical = true;
    else if (vertical && avail.x > avail.y * flip) vertical = false;

    if (vertical) {
        const float body_h = ImMax(1.0f, avail.y - thick);
        const float min_side = ImMin(ui.asset_browser.min_side, body_h * 0.5f);
        const float top_h = ImFloor(ImClamp(body_h * split_y, min_side, body_h - min_side));
        draw_tree(origin, avail.x, top_h, true);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + top_h + thick));
        draw_items_column(ctx, ImVec2(avail.x, body_h - top_h));
        return;
    }

    const float body_w = ImMax(1.0f, avail.x - thick);
    const float min_side = ImMin(ui.asset_browser.min_side, body_w * 0.5f);
    const float left_w = ImFloor(ImClamp(body_w * split_x, min_side, body_w - min_side));
    draw_tree(origin, left_w, avail.y, false);
    ImGui::SameLine(0, thick);
    draw_items_column(ctx, ImVec2(body_w - left_w, avail.y));
}

void ContentBrowserPanel::_draw_toolbar(EditorContext& ctx)
{
    (void)ctx;
    const float pad_x = ui_style().outliner.pad_x;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad_x);
    ui_search_bar("##search", search_buf, sizeof(search_buf), -pad_x);
}

ImVec2 ContentBrowserPanel::_item_size(float p_avail_w) const
{
    (void)p_avail_w;
    const UiStyle& ui = ui_style();
    const float w = ui.asset_browser.card_width;
    return ImVec2(w, ImFloor(w + ImGui::GetTextLineHeight() * 2.0f + ui.asset_browser.item_subtitle_gap + ui.asset_browser.card_pad * 2.0f));
}

AssetBrowser::ItemResult ContentBrowserPanel::_draw_item(EditorContext& ctx, const Item& p_item, ImVec2 p_size)
{
    const UiStyle& ui = ui_style();
    const float pad = ui.asset_browser.card_pad;
    const bool selected = _is_selected(*p_item.path);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + p_size.x, p0.y + p_size.y);

    ItemResult result;
    result.clicked = ImGui::InvisibleButton("##card", p_size);
    const bool hovered = ImGui::IsItemHovered();
    const bool context = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup) && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseReleased(ImGuiMouseButton_Right));
    if (!p_item.importing && context && !selected) _select_only(*p_item.path);
    ui_context_menu_push_style();
    if (!p_item.importing && ImGui::BeginPopupContextItem("##card_menu")) {
        const bool reimportable = std::any_of(selection.begin(), selection.end(), [](const std::filesystem::path& p) { return can_reimport(p); });
        const bool has_settings = std::any_of(selection.begin(), selection.end(), [](const std::filesystem::path& p) { return reimport_has_settings(reimport_target(p).kind); });
        if (ImGui::MenuItem(ICON_FA_ROTATE "  Reimport", nullptr, false, reimportable)) reimport_assets(ctx, selection);
        if (ImGui::MenuItem(ICON_FA_SLIDERS "  Reimport With Settings...", nullptr, false, has_settings)) reimport_with_settings(ctx, selection);
        const AssetInfo info = selection.size() == 1 ? read_asset_info(selection.front()) : AssetInfo{};
        if (info.valid() && ctx.references_dialog) {
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_MAGNIFYING_GLASS "  Find References")) ctx.references_dialog->open(ctx, info.guid, ReferencesDialog::Mode::FIND);
            if (ImGui::MenuItem(ICON_FA_RIGHT_LEFT "  Replace References...")) ctx.references_dialog->open(ctx, info.guid, ReferencesDialog::Mode::REPLACE);
        }
        ImGui::EndPopup();
    }
    ui_context_menu_pop_style();
    result.activated = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    if (!p_item.importing) _drag_source(p_item);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = selected ? ImGui::GetColorU32(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Header) : hovered ? ui.color.field_hover : ui.color.field;
    dl->AddRectFilled(p0, p1, bg);

    const ImVec2 thumb1(p1.x, p0.y + p_size.x);
    _draw_thumb(dl, p_item, p0, thumb1, ImGui::GetFontSize() * ui.asset_browser.card_icon_scale);

    const float name_x = p0.x + pad;
    const float name_max_x = p1.x - pad;
    const float name_y = thumb1.y + pad;
    const float type_y = name_y + ImGui::GetTextLineHeight() + ui.asset_browser.item_subtitle_gap;
    dl->PushClipRect(ImVec2(name_x, thumb1.y), ImVec2(name_max_x, p1.y), true);
    dl->AddText(ImVec2(name_x, name_y), ImGui::GetColorU32(ImGuiCol_Text), p_item.name);
    if (p_item.subtitle && *p_item.subtitle) dl->AddText(ImVec2(name_x, type_y), ImGui::GetColorU32(ImGuiCol_TextDisabled), p_item.subtitle);
    dl->PopClipRect();

    _draw_progress(dl, p_item, p0, p1);
    return result;
}

}
