#include <editor/assets/asset_field.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <cfloat>
#include <cstdio>

namespace lumen {

bool asset_field(EditorContext& ctx, const char* p_id, Guid& r_guid, AssetType p_type, const char* p_hint)
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
    // if (ImGui::Button(label, ImVec2(-FLT_MIN, 0.0f)) && r_guid != Guid{}) {
    //     r_guid = Guid{};
    //     changed = true;
    // }
    const float clear_w = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::Button(label, ImVec2(-(clear_w + spacing), 0.0f));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", asset ? asset->asset_path.c_str() : p_hint);
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

}