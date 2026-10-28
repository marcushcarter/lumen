#pragma once
#include <core/rendering/render_context.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/pass_category.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/base/error.h>
#include <string>

namespace lumen {

struct Feature
{
    const RenderContext* ctx = nullptr;
    std::string category = "?";
    bool enabled = true;

    static drivers::DeviceDriverVulkan::ImageCreateInfo _storage_image_info(const char* p_name, uint32_t p_width, uint32_t p_height, VkFormat p_format = VK_FORMAT_R16G16B16A16_SFLOAT);
    drivers::DeviceDriverVulkan::Pipeline _compute_pipeline(const wchar_t* p_resource, const char* p_name);
    virtual Error create_resources() { return Error::OK; }
    virtual Error create_pipelines() { return Error::OK; }
    virtual void destroy_resources() {}
    virtual void build(RenderGraph& g) = 0;

    virtual ~Feature() = default;
};

}
