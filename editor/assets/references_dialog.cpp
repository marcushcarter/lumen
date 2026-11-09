#include <editor/assets/references_dialog.h>
#include <editor/application/editor_context.h>
#include <editor/assets/asset_field.h>
#include <editor/assets/asset_registry.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace lumen {

void ReferencesDialog::open(EditorContext& ctx, Guid p_target, Mode p_mode)
{
    const AssetRegistry::Entry* e = ctx.assets ? ctx.assets->find(p_target) : nullptr;
    if (!e) return;
    active = true;
    mode = p_mode;
    target = p_target;
    type = e->type;
    name = e->name;
    refs = find_asset_references(ctx, p_target);
    checked.assign(refs.size(), 1);
    replacement = {};
    _warned_for = {};
    warnings.clear();
    delete_original = false;
}

void ReferencesDialog::draw(EditorContext& ctx)
{
    static constexpr const char* POPUP_ID = "###references_dialog";
    static constexpr ImGuiWindowFlags FLAGS = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
    if (!active) return;

    const UiStyle& ui = ui_style();
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    char title[64];
    std::snprintf(title, sizeof(title), "%s%s", mode == Mode::FIND ? "Find References" : "Replace References", POPUP_ID);
    if (!ImGui::IsPopupOpen(POPUP_ID)) ImGui::OpenPopup(POPUP_ID);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(ui.import_dialog.width, 0.0f), ImVec2(ui.import_dialog.width, vp->WorkSize.y * ui.import_dialog.max_height_ratio));
    if (!ImGui::BeginPopupModal(title, nullptr, FLAGS)) {
        active = false;
        return;
    }

    const bool replacing = mode == Mode::REPLACE;
    ui_field_push_style();
    ImGui::Text("%s  %s", asset_type_label(type), name.c_str());
    ImGui::TextDisabled(refs.empty() ? "Nothing references this asset." : refs.size() == 1 ? "Referenced in 1 place" : "Referenced in %zu places", refs.size());
    ImGui::Spacing();

    if (replacing) {
        asset_field(ctx, "Replace With", "###replace_with", replacement, type);
        if (replacement != _warned_for) {
            _warned_for = replacement;
            warnings = replacement.valid() && replacement != target ? replace_reference_warnings(ctx, target, replacement) : std::vector<std::string>{};
        }
        if (replacement == target) ui_warning("Pick a different asset to replace it with.");
        for (const std::string& w : warnings) ui_warning("%s", w.c_str());
        ImGui::Spacing();
    }

    if (!refs.empty()) {
        const float row_h = ImGui::GetFrameHeightWithSpacing();
        const float list_h = std::min((float)refs.size(), LIST_ROWS) * row_h + ImGui::GetStyle().WindowPadding.y * 2.0f;
        ImGui::BeginChild("##references", ImVec2(0.0f, list_h), ImGuiChildFlags_Borders);
        for (size_t i = 0; i < refs.size(); i++) {
            const AssetReference& r = refs[i];
            ImGui::PushID((int)i);
            if (replacing) {
                bool on = checked[i] != 0;
                if (ImGui::Checkbox("##use", &on)) checked[i] = on ? 1 : 0;
                ImGui::SameLine();
            }
            const char* kind = r.kind == AssetReference::Kind::PROJECT ? "Project" : asset_type_label(r.type);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(r.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s%s", kind, r.kind == AssetReference::Kind::OPEN_WORLD ? " (open, saved when you save the world)" : "");
            if (r.count > 1) {
                ImGui::SameLine();
                ImGui::TextDisabled("x%u", r.count);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", r.path.generic_string().c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    const bool all_checked = std::all_of(checked.begin(), checked.end(), [](uint8_t c) { return c != 0; });
    const bool any_checked = std::any_of(checked.begin(), checked.end(), [](uint8_t c) { return c != 0; });
    if (replacing) {
        if (!all_checked) {
            delete_original = false;
            ImGui::BeginDisabled();
        }
        ImGui::Checkbox("Delete the original afterwards", &delete_original);
        if (!all_checked) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(all_checked ? "Moves the original to the Recycle Bin once nothing uses it" : "Only possible when every reference is replaced");
    }
    ui_field_pop_style();

    ImGui::Dummy(ImVec2(0.0f, ui.space.lg));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, ui.space.sm));

    const ImGuiStyle& style = ImGui::GetStyle();
    const float button_w = ui.import_dialog.button_width;
    const float full = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + full - button_w * 2.0f - style.ItemSpacing.x);

    bool close = false;
    if (replacing) {
        const bool playing = ctx.pie_is_playing && ctx.pie_is_playing();
        const bool can_replace = replacement.valid() && replacement != target && any_checked && !playing;
        if (!can_replace) ImGui::BeginDisabled();
        ui_push_buttons(UiButton::PRIMARY);
        const bool accept = ImGui::Button("Replace", ImVec2(button_w, 0.0f));
        ui_pop_buttons();
        if (!can_replace) ImGui::EndDisabled();
        if (playing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Stop playing before replacing references");
        if (accept) {
            std::vector<AssetReference> chosen;
            for (size_t i = 0; i < refs.size(); i++) {
                if (checked[i]) chosen.push_back(refs[i]);
            }
            if (confirm_reference_tabs(ctx, chosen)) {
                const ReplaceResult result = replace_asset_references(ctx, target, replacement, chosen);
                if (delete_original && all_checked && result.failed == 0) delete_replaced_asset(ctx, target);
                close = true;
            }
        }
    } else {
        const bool can_switch = !refs.empty();
        if (!can_switch) ImGui::BeginDisabled();
        ui_push_buttons(UiButton::NEUTRAL);
        if (ImGui::Button("Replace...", ImVec2(button_w, 0.0f))) mode = Mode::REPLACE;
        ui_pop_buttons();
        if (!can_switch) ImGui::EndDisabled();
    }
    ImGui::SameLine();
    ui_push_buttons(UiButton::NEUTRAL);
    close |= ImGui::Button(replacing ? "Cancel" : "Close", ImVec2(button_w, 0.0f));
    ui_pop_buttons();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close = true;

    if (close) {
        active = false;
        refs.clear();
        checked.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

}
