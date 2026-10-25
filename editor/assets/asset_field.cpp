#include <editor/assets/asset_field.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
// #include <editor/drawer/drawer.h>
#include <editor/panels/shared_panels.h>
#include <editor/panels/content_browser_panel.h>
#include <drivers/imgui/imgui_driver.h>
#include <drivers/imgui/ui_style.h>
#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace lumen {

bool asset_field(EditorContext& ctx, const char* p_id, Guid& r_guid, AssetType p_type, const char* p_hint, const std::function<void()>& p_tooltip)
{
    bool changed = false;
    const AssetRegistry::Entry* asset = ctx.assets ? ctx.assets->find(r_guid) : nullptr;
    char label[160];
    if (asset) {
        snprintf(label, sizeof(label), "%s%s", asset->name.c_str(), p_id);
    } else {
        char guid[Guid::BUFFER];
        r_guid.to_chars(guid);
        snprintf(label, sizeof(label), "%s%s%s", guid, r_guid == Guid{} ? "" : " (missing)", p_id);
    }
    const float clear_w = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::Button(label, ImVec2(-(clear_w + spacing), 0.0f));
    if (ImGui::IsItemHovered() && !ImGui::GetDragDropPayload()) {
        if (asset && p_tooltip) {
            ImGui::BeginTooltip();
            p_tooltip();
            ImGui::EndTooltip();
        } else {
            ImGui::SetTooltip("%s", asset ? asset->asset_path.c_str() : p_hint);
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

    ImGui::SameLine(0.0f, spacing);
    ImGui::PushID(p_id);
    ImGui::BeginDisabled(r_guid == Guid{});
    if (ImGui::Button(ICON_FA_TRASH_CAN, ImVec2(clear_w, 0.0f))) {
        r_guid = Guid{};
        changed = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Clear");
    ImGui::PopID();
    return changed;
}

bool asset_thumbnail_field(EditorContext& ctx, const char* p_id, Guid& r_guid, AssetType p_type, const char* p_hint, const std::function<void()>& p_tooltip)
{
    const UiStyle& ui = ui_style();
    bool changed = false;
    const bool empty = r_guid == Guid{};
    const AssetRegistry::Entry* asset = !empty && ctx.assets ? ctx.assets->find(r_guid) : nullptr;

    const ImVec2 size(ASSET_THUMBNAIL_FIELD_SIZE, ASSET_THUMBNAIL_FIELD_SIZE);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImGui::InvisibleButton(p_id, size);
    const bool hovered = ImGui::IsItemHovered();

    VkImageView view = VK_NULL_HANDLE;
    // if (asset && ctx.drawer && ctx.drawer->content_browser) view = ctx.drawer->content_browser->thumbnail(ctx, r_guid, p_type);
    if (asset && ctx.shared_panels && ctx.shared_panels->content_browser) view = ctx.shared_panels->content_browser->thumbnail(ctx, r_guid, p_type);
    const VkDescriptorSet set = view && ctx.imgui ? ctx.imgui->texture_cache.get(view) : VK_NULL_HANDLE;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, p1, ui.color.sunken, ui.radius.sm);
    if (set) {
        dl->AddImageRounded((ImTextureID)set, p0, p1, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, ui.radius.sm);
    } else if (!asset) {
        const char* text = empty ? "na" : "missing";
        const ImVec2 t = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(std::floor(p0.x + (size.x - t.x) * 0.5f), std::floor(p0.y + (size.y - t.y) * 0.5f)), ui.color.text_dim, text);
    }
    if (hovered) dl->AddRect(p0, p1, ui.color.accent, ui.radius.sm);

    if (hovered && !ImGui::GetDragDropPayload()) {
        if (asset && p_tooltip) {
            ImGui::BeginTooltip();
            p_tooltip();
            ImGui::EndTooltip();
        } else if (asset) {
            ImGui::SetTooltip("%s", asset->asset_path.c_str());
        } else if (empty) {
            ImGui::SetTooltip("%s", p_hint);
        } else {
            char guid[Guid::BUFFER];
            r_guid.to_chars(guid);
            ImGui::SetTooltip("%s (missing)", guid);
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

    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x);
    ImGui::PushID(p_id);
    ImGui::BeginDisabled(empty);
    if (ImGui::Button(ICON_FA_TRASH_CAN, ImVec2(ImGui::GetFrameHeight(), 0.0f))) {
        r_guid = Guid{};
        changed = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Clear");
    ImGui::PopID();
    return changed;
}

}