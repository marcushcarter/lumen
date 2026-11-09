#include <editor/assets/asset_field.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/thumbnail_cache.h>
#include <editor/application/editor_context.h>
#include <core/rendering/renderer.h>
#include <core/base/strings.h>
#include <drivers/imgui/imgui_driver.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace lumen {

ImU32 asset_type_color(AssetType p_type)
{
    const UiStyle& ui = ui_style();
    switch (p_type) {
        case AssetType::TEXTURE: return ui.color.asset_texture;
        case AssetType::MESH: return ui.color.asset_mesh;
        case AssetType::MODEL: return ui.color.asset_model;
        case AssetType::MATERIAL: return ui.color.asset_material;
        case AssetType::SKELETON: return ui.color.asset_skeleton;
        case AssetType::ANIMATION: return ui.color.asset_animation;
        case AssetType::BLEND_SPACE: return ui.color.asset_blend_space;
        case AssetType::OVERLAY: return ui.color.asset_overlay;
        case AssetType::WORLD: return ui.color.asset_world;
        case AssetType::TERRAIN: return ui.color.asset_terrain;
        case AssetType::PREFAB: return ui.color.asset_prefab;
        default: return ImGui::GetColorU32(ImGuiCol_TextDisabled);
    }
}

const char* asset_type_label(AssetType p_type)
{
    switch (p_type) {
        case AssetType::TEXTURE: return "Texture";
        case AssetType::MESH: return "Mesh";
        case AssetType::MODEL: return "Model";
        case AssetType::MATERIAL: return "Material";
        case AssetType::SKELETON: return "Skeleton";
        case AssetType::ANIMATION: return "Animation";
        case AssetType::BLEND_SPACE: return "Blend Space";
        case AssetType::OVERLAY: return "Overlay";
        case AssetType::WORLD: return "World";
        case AssetType::TERRAIN: return "Terrain";
        case AssetType::PREFAB: return "Prefab";
        default: return "Asset";
    }
}

void asset_tooltip(EditorContext& ctx, Guid p_guid, AssetType p_type)
{
    const AssetRegistry::Entry* asset = ctx.assets ? ctx.assets->find(p_guid) : nullptr;
    if (!asset) return;
    const UiStyle& ui = ui_style();

    ImGui::BeginTooltip();
    const ImVec2 size(ui.asset_tooltip.thumbnail_size, ui.asset_tooltip.thumbnail_size);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ImGui::GetColorU32(ImGuiCol_FrameBg), ui.asset_tooltip.thumbnail_rounding);
    const VkImageView view = ctx.thumbnails ? ctx.thumbnails->get(ctx, p_guid, p_type) : VK_NULL_HANDLE;
    const VkDescriptorSet set = view && ctx.imgui ? ctx.imgui->texture_cache.get(view) : VK_NULL_HANDLE;
    if (set) dl->AddImageRounded((ImTextureID)set, p0, p1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, ui.asset_tooltip.thumbnail_rounding);
    ImGui::Dummy(size);

    ImGui::TextUnformatted(asset->name.c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, asset_type_color(p_type));
    ImGui::TextUnformatted(asset_type_label(p_type));
    ImGui::PopStyleColor();
    if (p_type == AssetType::TEXTURE && ctx.renderer) {
        if (const LTexture* t = ctx.renderer->textures.get(p_guid)) ImGui::TextDisabled("%u x %u", t->image.extent.width, t->image.extent.height);
    }
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (std::max)(size.x, ui.asset_tooltip.path_wrap_width));
    ImGui::TextDisabled("%s", asset->asset_path.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
}

bool asset_picker(EditorContext& ctx, const char* p_popup_id, Guid& r_guid, AssetType p_type)
{
    const UiStyle& ui = ui_style();
    ImGui::SetNextWindowSizeConstraints(ImVec2(ui.asset_picker.width, 0.0f), ImVec2(ui.asset_picker.width, ui.asset_picker.max_height));
    ui_context_menu_push_style();
    const bool open = ImGui::BeginPopup(p_popup_id);
    ui_context_menu_pop_style();
    if (!open) return false;

    static char search[128] = {};
    if (ImGui::IsWindowAppearing()) {
        search[0] = '\0';
        ImGui::SetKeyboardFocusHere();
    }
    ui_search_bar("##asset_picker_search", search, sizeof(search), -FLT_MIN);
    const std::string needle = str_lowered(search);

    struct Row { Guid guid; const AssetRegistry::Entry* entry; };
    std::vector<Row> rows;
    if (ctx.assets) {
        for (const auto& [guid, entry] : ctx.assets->entries) {
            if (entry.type != p_type) continue;
            if (!needle.empty() && str_lowered(entry.name).find(needle) == std::string::npos) continue;
            rows.push_back({ guid, &entry });
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.entry->name < b.entry->name; });

    bool picked = false;
    const float thumb = ui.asset_picker.thumbnail_size;
    ImGui::BeginChild("##asset_picker_list", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_None);
    if (rows.empty()) ImGui::TextDisabled(needle.empty() ? "No assets of this type" : "No matches");
    ImGuiListClipper clipper;
    clipper.Begin((int)rows.size(), thumb + ImGui::GetStyle().ItemSpacing.y);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
            const Row& row = rows[i];
            ImGui::PushID(i);
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##pick", row.guid == r_guid, ImGuiSelectableFlags_None, ImVec2(0.0f, thumb))) {
                r_guid = row.guid;
                picked = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) asset_tooltip(ctx, row.guid, p_type);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 t1(p0.x + thumb, p0.y + thumb);
            dl->AddRectFilled(p0, t1, ImGui::GetColorU32(ImGuiCol_FrameBg), ui.radius.sm);
            const VkImageView view = ctx.thumbnails ? ctx.thumbnails->get(ctx, row.guid, p_type) : VK_NULL_HANDLE;
            const VkDescriptorSet set = view && ctx.imgui ? ctx.imgui->texture_cache.get(view) : VK_NULL_HANDLE;
            if (set) dl->AddImageRounded((ImTextureID)set, p0, t1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, ui.radius.sm);
            const float text_y = p0.y + (thumb - ImGui::GetTextLineHeight()) * 0.5f;
            dl->AddText(ImVec2(t1.x + ui.space.lg, text_y), ImGui::GetColorU32(ImGuiCol_Text), row.entry->name.c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (picked) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return picked;
}

bool asset_field(EditorContext& ctx, const char* p_label, const char* p_id, Guid& r_guid, AssetType p_type)
{
    bool changed = false;
    const std::string_view type = asset_type_section(p_type);
    const AssetRegistry::Entry* asset = ctx.assets ? ctx.assets->find(r_guid) : nullptr;

    ImGui::PushID(p_id);
    if (p_label && p_label[0]) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s:", p_label);
        ImGui::SameLine();
    }

    char label[160];
    if (asset) {
        snprintf(label, sizeof(label), "%s##field", asset->name.c_str());
    } else if (!r_guid.valid()) {
        snprintf(label, sizeof(label), "No %.*s selected##field", (int)type.size(), type.data());
    } else {
        snprintf(label, sizeof(label), "Missing %.*s##field", (int)type.size(), type.data());
    }
    const float clear_w = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    if (ImGui::Button(label, ImVec2(-(clear_w + spacing), 0.0f))) ImGui::OpenPopup("##asset_picker");
    const ImVec2 field_min = ImGui::GetItemRectMin();
    const ImVec2 field_max = ImGui::GetItemRectMax();
    if (ImGui::IsItemHovered(asset ? ImGuiHoveredFlags_DelayNormal : ImGuiHoveredFlags_None) && !ImGui::GetDragDropPayload()) {
        if (asset) {
            asset_tooltip(ctx, r_guid, p_type);
        } else {
            ImGui::SetTooltip("Click to pick a %.*s, or drag one here from the Content Browser", (int)type.size(), type.data());
        }
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* peek = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE, ImGuiDragDropFlags_AcceptPeekOnly)) {
            if (((const AssetDragPayload*)peek->Data)->type == p_type) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(AssetDragPayload::TYPE)) {
                    r_guid = ((const AssetDragPayload*)payload->Data)->guid;
                    changed = true;
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::SetNextWindowPos(ImVec2(field_min.x, field_max.y + ui_style().menu.popup_offset), ImGuiCond_Appearing);
    if (asset_picker(ctx, "##asset_picker", r_guid, p_type)) changed = true;

    ImGui::SameLine(0.0f, spacing);
    ImGui::BeginDisabled(!r_guid.valid());
    ui_push_buttons(UiButton::GHOST);
    if (ImGui::Button(ICON_FA_TRASH_CAN "##clear", ImVec2(clear_w, 0.0f))) {
        r_guid = Guid{};
        changed = true;
    }
    ui_pop_buttons();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Clear");
    ImGui::PopID();
    return changed;
}

}
