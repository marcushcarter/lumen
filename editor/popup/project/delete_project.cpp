// #include <editor/popup/project/delete_project.h>
// #include <editor/popup/popup.h>
// #include <editor/project_manager/project_manager.h>
// #include <core/project/project.h>
// #include <core/io/path.h>
// #include <imgui.h>

// namespace lumen {

// void DeleteProjectPopup::open(const std::filesystem::path& p_path, std::string_view p_name)
// {
//     project_path = p_path;
//     project_name = p_name;
//     request = true;
// }

// void DeleteProjectPopup::draw(EditorContext& ctx)
// {
//     if (!popup_begin(NAME, request, ImVec2(500, 125))) return;

//     ImGui::Text("Permanently delete project \"%s\"?", project_name.c_str());
//     ImGui::SameLine();
//     ImGui::TextDisabled("%s", project_path.string().c_str());
//     ImGui::TextColored(ImVec4(0.86f, 0.35f, 0.35f, 1.0f), "This erases the project folder from disk and cannot be undone.");

//     const char* labels[] = { "Delete", "Cancel" };
//     switch (popup_footer(labels, 2)) {
//         case 0:
//             if (Project::destroy(project_path) == Error::Ok) {
//                 auto& recent = ctx.project_manager->recent;
//                 for (size_t i = 0; i < recent.size(); ++i)
//                     if (recent[i].path == project_path) { recent.erase(recent.begin() + i); break; }
//                 ctx.project_manager->save_recents();
//                 ctx.project_manager->selected = -1;
//             }
//             ImGui::CloseCurrentPopup();
//             break;
//         case 1: ImGui::CloseCurrentPopup(); break;
//     }

//     popup_end();
// }

// }
