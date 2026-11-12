#pragma once
#include <editor/layout/panel.h>
#include <IconsFontAwesome6.h>
#include <cstdint>

namespace lumen {

struct BlendSpaceTab;

struct BlendSpaceDetailsPanel : Panel
{
    static constexpr size_t NAME_SIZE = 64;
    static constexpr float AXIS_MARGIN = 0.25f;
    static constexpr float SAME_SAMPLE_EPSILON = 1.0e-3f;

    BlendSpaceTab* tab = nullptr;
    int32_t selected = -1;

    const char* name() const override { return ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT "  Blend Space"; }
    DockZone default_zone() const override { return DockZone::RIGHT_TOP; }
    bool pinned() const override { return true; }

    void _draw_pad(EditorContext& ctx);
    void _draw_samples(EditorContext& ctx);
    void _draw_selected(EditorContext& ctx);
    void _draw_overlays(EditorContext& ctx);
    void _draw_warnings(EditorContext& ctx);
    void draw_contents(EditorContext& ctx) override;
};

}
