// #include <editor/popup/project/new_project.h>
// #include <editor/popup/popup.h>
// #include <editor/project_manager/project_manager.h>
// #include <drivers/windows/dialogs_win32.h>
// #include <core/project/project.h>
// #include <core/io/path.h>
// #include <imgui.h>

// #include <string>
// #include <string_view>
// #include <cctype>
// #include <cstdio>

// namespace lumen {

// void NewProjectPopup::open()
// {
//     std::snprintf(name_buf, sizeof(name_buf), "Test Lumen");
//     std::snprintf(location_buf, sizeof(location_buf), "D:/");
//     create_folder = true;
//     edit_now = false;
//     request = true;
// }

// void NewProjectPopup::draw(EditorContext& ctx)
// {
//     if (!popup_begin(NAME, request, ImVec2(500, 200))) return;

//     const float label_w = 110.0f;
//     const float avail = ImGui::GetContentRegionAvail().x;
//     const float icon_w = ImGui::GetFrameHeight();
//     const float browse_w = 90.0f;
//     const float spacing = ImGui::GetStyle().ItemSpacing.x;

//     ImGui::AlignTextToFramePadding();
//     ImGui::TextUnformatted("Project Name");
//     ImGui::SameLine(label_w);
//     ImGui::SetNextItemWidth(avail - label_w);
//     ImGui::InputText("##proj_name", name_buf, sizeof(name_buf));

//     ImGui::AlignTextToFramePadding();
//     ImGui::TextUnformatted("Location");
//     ImGui::SameLine(label_w);
//     const float field_w = avail - label_w - icon_w - browse_w - spacing * 2.0f;
//     ImGui::SetNextItemWidth(field_w);
//     ImGui::InputText("##proj_loc", location_buf, sizeof(location_buf));

//     std::string_view nm = name_buf;
//     std::string_view loc = location_buf;

//     const char* message = "";
//     int status = 0;
//     can_create = false;
//     std::error_code ec;

//     if (nm.empty()) {
//         message = "Project name cannot be empty";
//     } else if (loc.empty()) {
//         message = "Choose a location";
//     } else if (!std::filesystem::is_directory(location_buf, ec)) {
//         message = "Location does not exist";
//     } else {
//         std::string folder;
//         bool new_word = true;
//         for (char c : nm) {
//             if (std::isalnum((unsigned char)c)) { folder += new_word ? (char)std::toupper(c) : c; new_word = false; }
//             else new_word = true;
//         }
//         final_path = create_folder ? std::filesystem::path(location_buf) / folder : std::filesystem::path(location_buf);
//         if (std::filesystem::exists(final_path / Project::FILE_NAME, ec)) {
//             message = "This folder already contains a project";
//         } else if (create_folder && std::filesystem::exists(final_path, ec) && !std::filesystem::is_empty(final_path, ec)) {
//             message = "A non-empty folder with that name already exists";
//         } else if (!create_folder && std::filesystem::exists(final_path, ec) && !std::filesystem::is_empty(final_path, ec)) {
//             status = 1;
//             can_create = true;
//             message = "Folder is not empty; project files will be added";
//         } else {
//             status = 2;
//             can_create = true;
//             message = create_folder ? "Project folder will be created" : "Valid empty folder";
//         }
//     }

//     ImU32 dot = status == 2 ? IM_COL32(70, 200, 90, 255) : status == 1 ? IM_COL32(230, 180, 50, 255) : IM_COL32(220, 70, 70, 255);
//     ImGui::SameLine();
//     ImVec2 dp = ImGui::GetCursorScreenPos();
//     ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(dp.x + icon_w * 0.5f, dp.y + icon_w * 0.5f), icon_w * 0.5f - 3.0f, dot);
//     ImGui::Dummy(ImVec2(icon_w, icon_w));

//     ImGui::SameLine();
//     if (ImGui::Button("Browse", ImVec2(browse_w, 0))) {
//         std::wstring picked = drivers::Win32Dialogs::open_folder(L"Choose project location");
//         if (!picked.empty()) {
//             std::string narrow = std::filesystem::path(picked).string();
//             std::snprintf(location_buf, sizeof(location_buf), "%s", narrow.c_str());
//         }
//     }

//     ImVec4 mc = status == 2 ? ImVec4(0.40f, 0.80f, 0.45f, 1.0f) : status == 1 ? ImVec4(0.90f, 0.72f, 0.25f, 1.0f) : ImVec4(0.86f, 0.35f, 0.35f, 1.0f);
//     ImGui::TextColored(mc, "%s", message);

//     ImGui::Spacing();

//     ImGui::Checkbox("Create project sub-folder", &create_folder);
//     ImGui::SameLine();
//     ImGui::TextDisabled("(?)");
//     if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: create <Location>/<ProjectName>\n" "Off: use the selected folder as the project root");
//     ImGui::Checkbox("Edit Now", &edit_now);

//     const char* labels[] = { "Create", "Cancel" };
//     unsigned mask = can_create ? 0u : 0b01u;
//     switch (popup_footer(labels, 2, mask)) {
//         case 0:
//             if (Project::create(final_path, std::string(name_buf)) == Error::Ok) {
//                 if (edit_now) ctx.open_project_callback(final_path);
//                 ctx.project_manager->add_recent(final_path, name_buf);
//                 ImGui::CloseCurrentPopup();
//             }
//             break;
//         case 1: ImGui::CloseCurrentPopup(); break;
//     }

//     popup_end();
// }

// }
