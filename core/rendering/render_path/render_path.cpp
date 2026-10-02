#include <core/rendering/render_path/render_path.h>

namespace lumen {

Error RenderPath::create_resources()
{
    using enum Error;
    if (features.empty()) log_write("RenderPath: no features registered.");

    created_count = 0;
    for (size_t i = 0; i < features.size(); ++i) {
        features[i]->ctx = &ctx;
        if (Error e = features[i]->create_resources(); e != OK) {
            destroy_resources();
            return e;
        }
        created_count = static_cast<uint32_t>(i + 1);
    }

    ctx.graph->begin(0);
    for (Feature* f : features) f->build(*ctx.graph);
    ctx.graph->begin(0);

    for (uint32_t i = 0; i < created_count; ++i) {
        if (Error e = features[i]->create_pipelines(); e != OK) {
            destroy_resources();
            return e;
        }
    }
    return OK;
}

void RenderPath::destroy_resources()
{
    for (uint32_t i = created_count; i-- > 0; ) features[i]->destroy_resources();
    created_count = 0;
}

void RenderPath::build(RenderGraph& g)
{
    for (Feature* f : features) {
        if (f->enabled) f->build(g);
    }
}

}