#pragma once
#include <core/rendering/render_path/scene_render_path.h>
#include <core/rendering/features/editor/debug_view_feature.h>
#include <core/rendering/features/editor/pick_feature.h>
#include <core/rendering/features/present/imgui_feature.h>
#include <core/rendering/features/present/screenshot_feature.h>

namespace lumen {

struct EditorRenderPath : SceneRenderPath
{
    DebugViewFeature debug;
    PickFeature pick;

    ImGuiFeature ui;
    ScreenshotFeature screenshot;    

    EditorRenderPath() {
        ui.viewport = "Viewport";
        features.push_back(&debug);
        features.push_back(&pick);
        features.push_back(&ui);
        features.push_back(&screenshot);
    }
};

}