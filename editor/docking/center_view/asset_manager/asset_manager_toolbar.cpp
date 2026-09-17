#include <editor/docking/center_view/asset_manager/asset_manager_toolbar.h>
#include <editor/editor_resources.h>
#include <core/io/path.h>
#include <drivers/imgui/imgui_driver.h>
#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <cstdio>
#include <cstdint>

#include <drivers/windows/dialogs_win32.h>
#include <core/project/project.h>
#include <editor/assets/asset_import_any.h>

namespace lumen {

void AssetBrowserToolbar::_breadcrumb(const std::filesystem::path& root, std::filesystem::path& selected)
{
    std::vector<std::filesystem::path> chain;
    chain.push_back(root); 
    if (!selected.empty()) {
        std::filesystem::path accum = root;
        for (const auto& part : std::filesystem::relative(selected, root)) {
            if (part == ".") continue;
            accum /= part;
            chain.push_back(accum);
        }
    }

    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    for (size_t k = 0; k < chain.size(); ++k) {
        if (k > 0) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled("/");
            ImGui::SameLine(0.0f, 4.0f);
        }
        
        const bool is_last = (k + 1 == chain.size());
        const std::string name = chain[k].filename().string();

        ImGui::PushID((int)k);
        ImGui::PushStyleColor(ImGuiCol_Text, is_last ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
        if (ImGui::Button(name.c_str())) selected = chain[k];
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::PopStyleColor();
}

void AssetBrowserToolbar::draw_header(EditorContext& ctx, const std::filesystem::path& root, std::filesystem::path& selected, char* search_buf, size_t search_cap)
{
    ImGui::BeginDisabled(!(!selected.empty() && selected != root));
    if (ImGui::Button(ICON_FA_CHEVRON_LEFT)) selected = selected.parent_path();
    ImGui::EndDisabled();
    
    ImGui::SameLine();
    if (ImGui::Button("New Folder")) {
        std::filesystem::create_directory(ctx.project->assets_dir / "New Folder");
    }
    
    ImGui::SameLine();
    if (ImGui::Button("+ Import")) {
        if (!selected.empty() && std::filesystem::exists(selected)) {
            std::vector<std::wstring> files = drivers::Win32Dialogs::open_files(
                L"All Supported\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0"
                L"Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0"
                L"All Files\0*.*\0"
            );
            for (const std::wstring& f : files) {
                const std::filesystem::path source = f;
                if (!asset_import_any(ctx, source, selected)) log_write("Skipped unsupported import: %s", source.string().c_str());
            }
        }
    }
    
    ImGui::SameLine();
    ImGui::InputTextWithHint("##search", "Search..", search_buf, search_cap);
    
    ImGui::SameLine();
    _breadcrumb(root, selected);
}

}