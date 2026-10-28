#pragma once
#include <editor/tabs/editor_tab.h>

namespace lumen {

struct EditorSettingsTab : EditorTab
{
    static constexpr TabKind KIND = TabKind::EDITOR_SETTINGS;

    const char* title() const override { return "Editor Settings"; }
    void on_update(EditorContext& ctx, float p_dt) override;

    void _apply_theme(EditorContext& ctx);
};

}
