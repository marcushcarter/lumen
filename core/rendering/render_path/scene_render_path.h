#pragma once
#include <core/rendering/render_path/render_path.h>
#include <core/rendering/features/geometry/geometry_feature.h>
#include <core/rendering/features/lighting/light_cull_feature.h>
#include <core/rendering/features/lighting/lighting_feature.h>

namespace lumen {

struct SceneRenderPath : RenderPath
{
    GeometryFeature geometry;
    LightCullFeature light_cull;
    LightingFeature lighting;
    
    SceneRenderPath() {
        features.push_back(&geometry);
        features.push_back(&light_cull);
        features.push_back(&lighting);
    }
};
    
}