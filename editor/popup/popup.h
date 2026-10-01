// #pragma once
// #include <imgui.h>

// namespace lumen {

// // Usage: if (!popup_begin(...)) return; <body> switch (popup_footer(...)) {...} popup_end();
// // Call ImGui::CloseCurrentPopup() between popup_footer and popup_end to close.

// inline bool popup_begin(const char* p_name, bool& p_request, ImVec2 p_size)
// {
//     if (p_request) {
//         ImGui::OpenPopup(p_name);
//         p_request = false;
//     }

//     ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
//     ImGui::SetNextWindowSize(p_size, ImGuiCond_Appearing);
//     if (!ImGui::BeginPopupModal(p_name, nullptr, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) return false;

//     const float footer_h = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y;
//     ImGui::BeginChild("##popup_body", ImVec2(0, -footer_h));
//     return true;
// }

// // Closes the body child, draws centered buttons, returns clicked index or -1.
// inline int popup_footer(const char* const* p_labels, int p_count, unsigned p_disabled_mask = 0, float p_button_w = 120.0f)
// {
//     ImGui::EndChild();

//     ImGuiStyle& s = ImGui::GetStyle();
//     float total = p_count * p_button_w + s.ItemSpacing.x * (p_count - 1);
//     float avail = ImGui::GetContentRegionAvail().x;
//     if (total < avail) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - total) * 0.5f);

//     int clicked = -1;
//     for (int i = 0; i < p_count; ++i) {
//         if (i) ImGui::SameLine();
//         ImGui::PushID(i);
//         bool dis = (p_disabled_mask >> i) & 1u;
//         if (dis) ImGui::BeginDisabled();
//         if (ImGui::Button(p_labels[i], ImVec2(p_button_w, 0))) clicked = i;
//         if (dis) ImGui::EndDisabled();
//         ImGui::PopID();
//     }
//     return clicked;
// }

// inline void popup_end()
// {
//     ImGui::EndPopup();
// }

// }
