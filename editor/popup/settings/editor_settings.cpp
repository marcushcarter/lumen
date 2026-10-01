// #include <editor/popup/settings/editor_settings.h>
// #include <editor/popup/popup.h>
// #include <editor/editor_settings.h>
// #include <drivers/windows/window_driver_win32.h>
// #include <imgui.h>
// #include <IconsFontAwesome6.h>

// namespace lumen {

// void EditorSettingsPopup::draw(EditorContext& ctx)
// {
//     if (!popup_begin(NAME, request, ImVec2(500, 300))) return;

//     Theme& t = ctx.settings->theme;
//     bool changed = false;

//     if (ImGui::BeginCombo("Preset", Theme::theme_preset_name(t.preset))) {
//         for (int i = 0; i < (int)std::size(Theme::THEME_PRESETS); ++i) {
//             if (ImGui::Selectable(Theme::THEME_PRESETS[i].name, t.preset == i)) {
//                 t.preset  = i;
//                 t.base = Theme::THEME_PRESETS[i].base;
//                 t.accent = Theme::THEME_PRESETS[i].accent;
//                 t.text = Theme::THEME_PRESETS[i].text;
//                 changed = true;
//             }
//         }
//         if (ImGui::Selectable("Custom", t.preset == -1)) { t.preset = -1; changed = true; }
//         ImGui::EndCombo();
//     }

//     if (ImGui::ColorEdit3("Base", &t.base.x)) { t.preset = -1; changed = true; }
//     if (ImGui::ColorEdit3("Text", &t.text.x)) { t.preset = -1; changed = true; }
//     ImGui::BeginDisabled(t.use_system_accent);
//     if (ImGui::ColorEdit3("Accent", &t.accent.x)) { t.preset = -1; changed = true; }
//     ImGui::EndDisabled();
//     if (ImGui::Checkbox("Use system accent", &t.use_system_accent)) changed = true;
//     if (ImGui::Button("Reset to defaults")) {
//         t = Theme{};
//         changed = true;
//     }

//     if (changed) {
//         t.apply();
//         ImVec4 titlebar = ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg];
//         ctx.win32->window_set_titlebar_color(RGB((BYTE)(titlebar.x * 255), (BYTE)(titlebar.y * 255), (BYTE)(titlebar.z * 255)));
//     }

//     bool custom = ctx.win32->window.custom_titlebar;
//     if (ImGui::Checkbox("Window Custom Titlebar", &custom)) ctx.win32->window_set_custom_titlebar(custom);

//     const char* labels[] = { "Close" };
//     if (popup_footer(labels, 1) == 0) ImGui::CloseCurrentPopup();
//     popup_end();
// }

// }
