#include <editor/popups/unsaved_changes_popup.h>
#include <editor/tabs/tab_manager.h>
#include <imgui.h>

namespace lumen {

void UnsavedChangesPopup::draw(EditorContext& ctx)
{
    if (!popup_begin(NAME, open, ImVec2(440, 130))) return;

    EditorTab* t = ctx.tabs->find(kind, guid);
    if (!t) {
        ImGui::EndChild();
        ImGui::CloseCurrentPopup();
        popup_end();
        return;
    }

    ImGui::TextWrapped("Save changes to \"%s\" before closing?", t->title());
    if (save_failed) ImGui::TextColored(ImVec4(0.86f, 0.35f, 0.35f, 1.0f), "Save failed.");

    const char* labels[] = { "Save", "Discard", "Cancel" };
    switch (popup_footer(labels, 3)) {
        case 0:
            if (t->save(ctx) == Error::OK) {
                ctx.tabs->close(kind, guid, true);
                ImGui::CloseCurrentPopup();
            } else {
                save_failed = true;
            }
            break;
        case 1:
            ctx.tabs->close(kind, guid, true);
            ImGui::CloseCurrentPopup();
            break;
        case 2:
            ImGui::CloseCurrentPopup();
            break;
    }

    popup_end();
}

}
