#include <editor/assets/asset_field.h>
#include <editor/assets/asset_drag_payload.h>
#include <editor/assets/asset_registry.h>
#include <editor/assets/thumbnail_cache.h>
#include <editor/application/editor_context.h>
#include <drivers/imgui/imgui_driver.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace lumen {

bool asset_field(EditorContext& ctx, const char* p_label, const char* p_id, Guid& r_guid, AssetType p_type, const std::function<void()>& p_tooltip)
{
    bool changed = false;
    const std::string_view type = asset_type_section(p_type);
    const AssetRegistry::Entry* asset = ctx.assets ? ctx.assets->find(r_guid) : nullptr;

    ImGui::PushID(p_id);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s:", p_label);
    ImGui::SameLine();

    char label[160];
    if (asset) {
        snprintf(label, sizeof(label), "%s##field", asset->name.c_str());
    } else if (!r_guid.valid()) {
        snprintf(label, sizeof(label), "No %.*s selected##field", (int)type.size(), type.data());
    } else {
        char guid[Guid::BUFFER];
        r_guid.to_chars(guid);
        snprintf(label, sizeof(label), "%s (missing)##field", guid);
    }
    const float clear_w = ImGui::GetFrameHeight();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::Button(label, ImVec2(-(clear_w + spacing), 0.0f));
    if (ImGui::IsItemHovered() && !ImGui::GetDragDropPayload()) {
        if (asset && p_tooltip) {
            ImGui::BeginTooltip();
            p_tooltip();
            ImGui::EndTooltip();
        } else if (asset) {
            ImGui::SetTooltip("%s", asset->asset_path.c_str());
        } else {
            ImGui::SetTooltip("Drag a %.*s here from Content Browser", (int)type.size(), type.data());
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
