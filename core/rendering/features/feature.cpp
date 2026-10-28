#include <core/rendering/features/feature.h>
#include <core/io/embedded_resource.h>

namespace lumen {

drivers::DeviceDriverVulkan::ImageCreateInfo Feature::_storage_image_info(const char* p_name, uint32_t p_width, uint32_t p_height, VkFormat p_format)
{
    drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
    ci.name = p_name;
    ci.format = p_format;
    ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sizing = drivers::DeviceDriverVulkan::ImageCreateInfo::Sizing::FIXED;
    ci.fixed_width = p_width;
    ci.fixed_height = p_height;
    return ci;
}

drivers::DeviceDriverVulkan::Pipeline Feature::_compute_pipeline(const wchar_t* p_resource, const char* p_name)
{
    EmbeddedResource::Blob blob = EmbeddedResource::load(p_resource);
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = p_name });
    drivers::DeviceDriverVulkan::Pipeline pipe = ctx->dd->compute_pipeline_create({ cs, p_name });
    ctx->dd->shader_free(cs);
    return pipe;
}

}
