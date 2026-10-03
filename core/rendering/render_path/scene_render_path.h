#pragma once
#include <core/rendering/render_path/render_path.h>
#include <core/rendering/features/geometry/geometry_feature.h>

namespace lumen {

struct SceneRenderPath : RenderPath
{
    GeometryFeature geometry;
    
    SceneRenderPath() {
        features.push_back(&geometry);
    }
};
    
}