#include <editor/docking/center_view/debug_tabs/output_tab.h>
#include <drivers/windows/dialogs_win32.h>
#include <core/base/error.h>
#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <fstream>

namespace lumen {

void OutputDebugTab::draw(EditorContext&)
{    
    LogSink& sink = log_sink();

    float input_height = ImGui::GetFrameHeightWithSpacing();

    ImGui::PushTextWrapPos(0.0f);
    ImGui::BeginChild("##log_region", ImVec2(0, -input_height), true);
    {
        for (const std::string& line : sink.lines) ImGui::TextUnformatted(line.c_str());
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    ImGui::PopTextWrapPos();

    static char input_buffer[512] = {0};
    bool enter_pressed = false;

    float button_w = 70.0f;
    int button_count = 3;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float reserved = button_count * (button_w + spacing);
    ImGui::PushItemWidth(-reserved);

    if (ImGui::InputTextWithHint("##console_input", "Enter a command", input_buffer, IM_ARRAYSIZE(input_buffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (input_buffer[0] != '\0') {
            log_write("> %s", input_buffer);
            // command dispatch
            input_buffer[0] = '\0';
        }
        enter_pressed = true;
    }
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Copy", ImVec2(button_w, 0))) {
        std::string all = sink.to_string();
        ImGui::SetClipboardText(all.c_str());
    }

    ImGui::SameLine();
    if (ImGui::Button("Export", ImVec2(button_w, 0))) {
        std::wstring path = drivers::Win32Dialogs::save_file(L"Text Files\0*.txt\0All Files\0*.*\0\0", L"txt");
        if (!path.empty()) {
            std::ofstream f(path);
            if (f) {
                f << sink.to_string();
                log_write("Exported base to file");
            } else {
                log_write("Failed to write base export");
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(button_w, 0))) {
        sink.clear();
    }

    if (enter_pressed) ImGui::SetKeyboardFocusHere(-1);
}
    
}