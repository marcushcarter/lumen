#pragma once
#include <core/rendering/render_context.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/pass_category.h>
#include <core/base/error.h>
#include <string>

namespace lumen {

struct Feature
{
    const RenderContext* ctx = nullptr;
    
    std::string category = "?";
    bool enabled = true;

    virtual Error create_resources() { return Error::OK; };
    virtual Error create_pipelines() { return Error::OK; }
    virtual void destroy_resources() {}
    virtual void build(RenderGraph& g) = 0;
    
    virtual ~Feature() = default;
};

}