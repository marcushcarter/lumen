#pragma once
#include <editor/popups/popup.h>
#include <editor/tabs/editor_tab.h>

namespace lumen {

struct UnsavedChangesPopup : Popup
{
    static constexpr const char* NAME = "Unsaved Changes";

    TabKind kind = TabKind::WORLD;
    Guid guid;
    bool save_failed = false;

    const char* name() const override { return NAME; }
    void draw(EditorContext& ctx) override;
};

}
