#include <editor/docking/center_view/debugger.h>
#include <editor/docking/center_view/output/output.h>
#include <editor/docking/center_view/profiler/profiler.h>
#include <editor/docking/center_view/memory/memory.h>
#include <editor/docking/center_view/asset_manager/asset_manager.h>
#include <core/base/error.h>
#include <cfloat>

namespace lumen {

void Debugger::initialize()
{
    tabs.push_back(std::make_unique<AssetManagerDebugTab>());
    tabs.push_back(std::make_unique<ProfilerDebugTab>());
    tabs.push_back(std::make_unique<MemoryDebugTab>());
    tabs.push_back(std::make_unique<OutputDebugTab>());
}

void Debugger::draw_content(EditorContext& ctx)
{
    if (active >= 0 && active < (int)tabs.size()) tabs[active]->draw(ctx);
}

void Debugger::draw_strip(EditorContext&)
{
    for (int i = 0; i < (int)tabs.size(); ++i) {
        if (i > 0) ImGui::SameLine(0.0f, 4.0f);
        bool is_active = (i == active);
        if (is_active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(tabs[i]->name())) {
            if (is_active) collapsed = !collapsed;
            else { active = i; collapsed = false; }
        }
        if (is_active) ImGui::PopStyleColor();
    }
}
    
}