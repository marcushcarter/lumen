#pragma once
#include <core/rendering/render_graph.h>

namespace lumen {

inline const char* editor_overlay_target(RenderGraph& r_graph)
{
    if (r_graph.image_resource("Viewport")) return "Viewport";
    if (r_graph.image_resource("DisplayColor")) return "DisplayColor";
    return nullptr;
}

}
