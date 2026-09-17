#include <editor/docking/center_view/asset_manager/asset_manager.h>

// #include <editor/asset_manager/asset_manager.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/project/project.h>
#include <core/io/path.h>
#include <editor/editor_resources.h>
#include <drivers/imgui/imgui_driver.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>

#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <imgui.h>
#include <implot.h>

namespace lumen {

void AssetManagerDebugTab::_draw_folder_node(const std::filesystem::path& dir, std::filesystem::path& selected, int depth)
{
    ImGui::PushID(dir.string().c_str());

    ImGuiStorage* storage  = ImGui::GetStateStorage();
    const ImGuiID open_key = ImGui::GetID("open");
    bool open = storage->GetBool(open_key, depth == 0);

    std::vector<std::filesystem::path> subs;
    bool has_children;
    if (open) {
        Paths::gather_subdirs(dir, subs);
        has_children = !subs.empty();
    } else {
        has_children = Paths::has_subdir(dir);
    }
    if (!has_children) open = false;

    float indent_w = 14.0f;
    const float row_h = ImGui::GetTextLineHeight() + 4.0f;
    const float full_w = ImGui::GetContentRegionAvail().x;
    const ImVec2 row_min = ImGui::GetCursorScreenPos();
    const ImVec2 row_max = ImVec2(row_min.x + full_w, row_min.y + row_h);
    const float indent = depth * indent_w;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool selected_here = (selected == dir);
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
    if (ImGui::InvisibleButton("row", ImVec2(body_w, row_h))) selected = dir;
    if (has_children && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        open = !open; storage->SetBool(open_key, open);
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            Paths::move((const char*)payload->Data, dir);
        ImGui::EndDragDropTarget();
    }

    const char* icon = open ? ICON_FA_FOLDER_OPEN : ICON_FA_FOLDER;
    const ImVec2 is = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(body_x, row_min.y + (row_h - is.y) * 0.5f), IM_COL32(224, 187, 88, 255), icon);

    float icon_gap = 6.0f;
    const std::string name = dir.filename().string();
    const float name_x = body_x + is.x + icon_gap;
    dl->PushClipRect(ImVec2(name_x, row_min.y), row_max, true);
    dl->AddText(ImVec2(name_x, row_min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
    dl->PopClipRect();

    ImGui::SetCursorScreenPos(ImVec2(row_min.x, row_max.y));
    if (open) for (const auto& sub : subs) _draw_folder_node(sub, selected, depth + 1);

    ImGui::PopID();
}

void AssetManagerDebugTab::draw(EditorContext& ctx)
{
    if (selected_folder.empty()) selected_folder = ctx.project->assets_dir;
    const std::filesystem::path& root = ctx.project->assets_dir;
    
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
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return;
    _draw_folder_node(root, selected_folder, 0);
    ImGui::EndChild();

    ImGui::SameLine(0, 0);
    SplitterState sx = imgui_splitter("##am_split_lr", SplitAxis::X, ImVec2(thick, avail.y));
    if (sx.active) split_x += sx.delta / body_w;
    ImGui::SameLine(0, 0);

    ImGui::BeginChild("##right", ImVec2(right_w, avail.y), true);
    toolbar.draw_header(ctx, root, selected_folder, search_buf, sizeof(search_buf));
    ImGui::BeginChild("##bottom_right", ImVec2(0, 0), true);
    list.draw(ctx, selected_folder, search_buf);
    ImGui::EndChild();
    ImGui::EndChild();
    
    const float divider_x = region_p0.x + left_w;
    const float shadow_w  = 20.0f;
    const ImU32 c_edge = IM_COL32(0, 0, 0, 80);
    const ImU32 c_fade = IM_COL32(0, 0, 0, 0);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilledMultiColor(ImVec2(divider_x - shadow_w, region_p0.y), ImVec2(divider_x, region_p0.y + avail.y), c_fade, c_edge, c_edge, c_fade);

}

}