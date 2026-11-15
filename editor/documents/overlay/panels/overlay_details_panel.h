#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>
#include <string>
#include <vector>

namespace lumen {

struct OverlayDocument;

struct OverlayDetailsPanel : Panel
{
    static constexpr int MAX_DEPTH = 16;

    OverlayDocument* tab = nullptr;

    const char* name() const override { return ICON_FA_LAYER_GROUP "  Overlay"; }

    bool _joint_picker(const char* p_popup_id, std::string& r_name);
    void _draw_joints(EditorContext& ctx, std::vector<std::string>& r_names, const char* p_add_label);
    void _draw_warnings(EditorContext& ctx);
    void draw_contents(EditorContext& ctx) override;
};

}
