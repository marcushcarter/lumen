#pragma once
#include <editor/docking/panel.h>
#include <core/world/world.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct DetailsPanel : Panel
{
    Entity rotation_entity = ENTITY_NULL;
    quat rotation_seen = quat(1.0f, 0.0f, 0.0f, 0.0f);
    vec3 rotation_euler = vec3(0.0f);

    const char* name() const override { return ICON_FA_IMAGE "  Details"; }
    DockZone default_zone() const override { return DockZone::RIGHT_BOTTOM; }
    void draw_contents(EditorContext& ctx) override;

    bool _component_begin(const char* p_title);
    bool _component_end(bool p_open, bool p_deletable = true);
};

}