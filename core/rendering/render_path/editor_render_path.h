#pragma once
#include <core/rendering/render_path/scene_render_path.h>
#include <core/rendering/features/editor/debug_view_feature.h>
#include <core/rendering/features/editor/pick_feature.h>
#include <core/rendering/features/editor/outline_feature.h>
#include <core/rendering/features/present/imgui_feature.h>

namespace lumen {

struct EditorRenderPath : SceneRenderPath
{
    DebugViewFeature debug;
    PickFeature pick;
    OutlineFeature outline;

    ImGuiFeature ui;

    void _register_features() override {
        SceneRenderPath::_register_features();
        ui.viewport = "Viewport";
        ui.viewport_fallback = "DisplayColor";
        features.push_back(&debug);
        features.push_back(&outline);
        features.push_back(&pick);
        features.push_back(&ui);
    }
};

}
