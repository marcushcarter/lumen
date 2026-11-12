#pragma once
#include <core/assets/lanimgraph.h>
#include <editor/tabs/asset_tab.h>
#include <editor/ui/ui_style.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct AnimGraphTab : AssetTab
{
    static constexpr TabKind KIND = TabKind::ANIM_GRAPH;
    static constexpr const char* ICON = ICON_FA_DIAGRAM_PROJECT;
    static constexpr float GRAPH_WIDTH = 2.0f / 3.0f;
    static constexpr float BOTTOM_HEIGHT = 0.5f;

    LAnimGraph graph;
    LAnimGraph saved;

    const char* _icon() const override { return ICON; }
    ImU32 icon_color() const override { return ui_style().color.asset_anim_graph; }
    const char* title() const override { return _title.empty() ? "Animation Graph" : _title.c_str(); }
    bool dirty() const override { return graph != saved; }
    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;
};

}
