#pragma once
#include <core/rendering/render_path/scene_render_path.h>
#include <core/rendering/features/debug_view_feature.h>
#include <core/rendering/features/imgui_feature.h>
#include <core/rendering/features/screenshot_feature.h>

namespace lumen {

struct EditorRenderPath : SceneRenderPath
{
    DebugViewFeature debug;
    ImGuiFeature ui;
    ScreenshotFeature screenshot;    

    EditorRenderPath() {
        ui.viewport = "Viewport";
        features.push_back(&debug);
        features.push_back(&ui);
        features.push_back(&screenshot);
    }
};

}