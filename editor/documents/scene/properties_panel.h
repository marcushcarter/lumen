#pragma once
#include <editor/layout/panel.h>
#include <core/scene/scene.h>
#include <IconsFontAwesome6.h>

namespace lumen {

struct PropertiesPanel : Panel
{
    Entity rotation_entity = ENTITY_NULL;
    quat rotation_seen = quat(1.0f, 0.0f, 0.0f, 0.0f);
    vec3 rotation_euler = vec3(0.0f);

    const char* name() const override { return "Properties"; }
    const char* icon() const override { return ICON_FA_TABLE_LIST; }
    ImVec2 window_padding() const override { return ImVec2(0.0f, 0.0f); }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }

    void draw_contents(EditorContext& ctx) override;
};

}
