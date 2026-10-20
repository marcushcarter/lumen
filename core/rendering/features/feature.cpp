#include <core/rendering/features/feature.h>
#include <core/io/embedded_resource.h>

namespace lumen {

drivers::DeviceDriverVulkan::Pipeline Feature::_compute_pipeline(const wchar_t* p_resource, const char* p_name)
{
    EmbeddedResource::Blob blob = EmbeddedResource::load(p_resource);
    VkShaderModule cs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::COMPUTE, .glsl = (const char*)blob.data, .glsl_size = blob.size, .name = p_name });
    drivers::DeviceDriverVulkan::Pipeline pipe = ctx->dd->compute_pipeline_create({ cs, p_name });
    ctx->dd->shader_free(cs);
    return pipe;
}

}