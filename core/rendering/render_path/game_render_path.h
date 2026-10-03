#pragma once
#include <core/rendering/render_path/scene_render_path.h>
#include <core/rendering/features/present/present_feature.h>

namespace lumen {

struct GameRenderPath : SceneRenderPath
{
    PresentFeature present; 

    GameRenderPath() {
        features.push_back(&present);
    }
};

}