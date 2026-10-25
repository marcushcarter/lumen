#include <editor/panels/output_panel.h>
#include <drivers/imgui/ui_style.h>
#include <drivers/windows/dialogs_win32.h>
#include <core/base/error.h>
#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <fstream>

namespace lumen {

void console_submit(const char* p_command)
{
    if (!p_command || p_command[0] == '\0') return;
    log_write("> %s", p_command);
}

void OutputPanel::draw_contents(EditorContext&)
{    
    LogSink& sink = log_sink();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::BeginChild("##log_region", ImVec2(0, 0), true);
    for (const std::string& line : sink.lines) ImGui::TextUnformatted(line.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::PopTextWrapPos();
}
    
}