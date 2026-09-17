#include <core/rendering/features/imgui_feature.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <drivers/imgui/imgui_driver.h>
#include <core/rendering/render_graph.h>
#include <core/base/error.h>

namespace lumen {

Error ImGuiFeature::create_resources()
{
    ui_pass.name = "ImGui_UI";
    ui_pass.category = "Present";
    ui_pass.setup = [this](RenderGraph::Builder& b) {
        b.color_attachment("Backbuffer", VK_ATTACHMENT_LOAD_OP_CLEAR, { { 0.1f, 0.1f, 0.1f, 1.0f } });
        if (viewport && b.graph->image_resource(viewport)) b.read_image(viewport, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    };
    ui_pass.execute = [this](RenderGraph::CommandList& cl) {
        ctx->imgui->record_commands(cl.cmd);
    };

    return Error::Ok;
};

void ImGuiFeature::build(RenderGraph& g)
{
    if (!enabled) return;
    g.add(&ui_pass);
};

}