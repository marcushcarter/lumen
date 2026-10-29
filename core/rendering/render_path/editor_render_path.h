#pragma once
#include <core/rendering/render_path/scene_render_path.h>
#include <core/rendering/features/editor/debug_view_feature.h>
#include <core/rendering/features/editor/grid_feature.h>
#include <core/rendering/features/editor/pick_feature.h>
#include <core/rendering/features/editor/outline_feature.h>
#include <core/rendering/features/present/imgui_feature.h>

namespace lumen {

struct EditorRenderPath : SceneRenderPath
{
    DebugViewFeature debug;
    GridFeature grid;
    PickFeature pick;
    OutlineFeature outline;

    ImGuiFeature ui;

    void _register_features() override {
        SceneRenderPath::_register_features();
        taa.enabled = false;
        ui.viewport = "Viewport";
        ui.viewport_fallback = "DisplayColor";
        features.push_back(&debug);
        features.push_back(&grid);
        features.push_back(&outline);
        features.push_back(&pick);
        features.push_back(&ui);
    }
};

}
