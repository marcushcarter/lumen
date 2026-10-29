#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>
#include <cstdint>
#include <string>

namespace lumen {

struct ProjectSettingsPanel : Panel
{
    static constexpr uint32_t MAX_TITLE = 256;

    char title_buf[MAX_TITLE] = {};
    char export_folder_buf[MAX_TITLE] = {};
    char export_exe_buf[MAX_TITLE] = {};

    bool _text_setting(const char* p_label, const char* p_hint, const char* p_tooltip, char* r_buf, std::string& r_value);

    const char* name() const override { return ICON_FA_WRENCH "  Project Settings"; }
    DockZone default_zone() const override { return DockZone::RIGHT_BOTTOM; }
    bool default_open() const override { return false; }
    void draw_contents(EditorContext& ctx) override;
};

}
