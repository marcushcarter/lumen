#include <editor/assets/texture_viewer.h>
#include <editor/application/editor_context.h>
#include <drivers/imgui/imgui_driver.h>
#include <core/rendering/renderer.h>
#include <vulkan/vk_enum_string_helper.h>
#include <cstdio>
#include <cstring>

namespace lumen {

using Image = drivers::DeviceDriverVulkan::Image;

uint32_t TextureViewer::channel_count(VkFormat p_format)
{
    switch (p_format) {
        case VK_FORMAT_BC4_UNORM_BLOCK:
        case VK_FORMAT_BC4_SNORM_BLOCK:
        case VK_FORMAT_R8_UNORM:
        case VK_FORMAT_R8_SRGB:
            return 1;
        case VK_FORMAT_BC5_UNORM_BLOCK:
        case VK_FORMAT_BC5_SNORM_BLOCK:
        case VK_FORMAT_R8G8_UNORM:
        case VK_FORMAT_R8G8_SRGB:
            return 2;
        case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case VK_FORMAT_BC6H_SFLOAT_BLOCK:
            return 3;
        default:
            return 4;
    }
}

uint8_t TextureViewer::channel_mask(uint32_t p_channels)
{
    uint8_t mask = CHANNEL_R;
    if (p_channels >= 2) mask |= CHANNEL_G;
    if (p_channels >= 3) mask |= CHANNEL_B;
    if (p_channels >= 4) mask |= CHANNEL_A;
    return mask;
}

VkComponentMapping TextureViewer::swizzle(uint8_t p_mask)
{
    const VkComponentSwizzle zero = VK_COMPONENT_SWIZZLE_ZERO;
    const VkComponentSwizzle one = VK_COMPONENT_SWIZZLE_ONE;
    const VkComponentSwizzle from[4] = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A };

    uint32_t count = 0;
    VkComponentSwizzle only = zero;
    for (uint32_t i = 0; i < 4; i++) {
        if (!(p_mask & (1u << i))) continue;
        count++;
        only = from[i];
    }
    // A single channel reads best as grayscale.
    if (count == 0) return { zero, zero, zero, one };
    if (count == 1) return { only, only, only, one };
    // Two-channel normal maps have no stored Z; a flat Z of one gives the usual blue look.
    const VkComponentSwizzle b_missing = (p_mask & VIEW_NORMAL) ? one : zero;
    return {
        (p_mask & CHANNEL_R) ? from[0] : zero,
        (p_mask & CHANNEL_G) ? from[1] : zero,
        (p_mask & CHANNEL_B) ? from[2] : b_missing,
        (p_mask & CHANNEL_A) ? from[3] : one,
    };
}

void TextureViewer::format_name(VkFormat p_format, char* r_out, size_t p_size)
{
    const char* name = string_VkFormat(p_format);
    if (std::strncmp(name, "VK_FORMAT_", 10) == 0) name += 10;
    std::snprintf(r_out, p_size, "%s", name);
    const size_t len = std::strlen(r_out);
    if (len > 6 && std::strcmp(r_out + len - 6, "_BLOCK") == 0) r_out[len - 6] = '\0';
}

Error TextureViewer::initialize(drivers::DeviceDriverVulkan& r_dd)
{
    using enum Error;
    dd = &r_dd;

    drivers::DeviceDriverVulkan::SamplerCreateInfo ci{};
    ci.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.filter = VK_FILTER_LINEAR;
    ci.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    ci.name = "texture_viewer_linear";
    sampler_linear = dd->sampler_create(ci);
    LUMEN_ERR_FAIL_COND_V(sampler_linear.sampler == VK_NULL_HANDLE, FAILED);

    ci.filter = VK_FILTER_NEAREST;
    ci.mipmap_mode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.name = "texture_viewer_nearest";
    sampler_nearest = dd->sampler_create(ci);
    LUMEN_ERR_FAIL_COND_V(sampler_nearest.sampler == VK_NULL_HANDLE, FAILED);

    return OK;
}

void TextureViewer::shutdown()
{
    if (!dd) return;
    for (const Retired& r : _retired) vkDestroyImageView(dd->device, r.view, nullptr);
    _retired.clear();
    dd->sampler_free(sampler_linear);
    dd->sampler_free(sampler_nearest);
    dd = nullptr;
}

VkImageView TextureViewer::create_view(const Image& p_image, int32_t p_mip, uint8_t p_mask)
{
    VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ci.image = p_image.image;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci.format = p_image.format;
    ci.components = swizzle(p_mask);
    ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, p_mip < 0 ? 0u : (uint32_t)p_mip, p_mip < 0 ? p_image.mip_levels : 1u, 0, 1 };
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(dd->device, &ci, nullptr, &view) != VK_SUCCESS) return VK_NULL_HANDLE;
    return view;
}

void TextureViewer::retire(EditorContext& ctx, VkImageView p_view)
{
    ctx.imgui->texture_cache.retire(p_view, ctx.renderer->frame_number, ctx.renderer->frame_count);
    _retired.push_back({ p_view, ctx.renderer->frame_number });
}

void TextureViewer::collect(uint64_t p_frame_number, uint32_t p_frame_count)
{
    for (size_t i = 0; i < _retired.size();) {
        if (p_frame_number >= _retired[i].frame + p_frame_count) {
            vkDestroyImageView(dd->device, _retired[i].view, nullptr);
            _retired[i] = _retired.back();
            _retired.pop_back();
        } else {
            ++i;
        }
    }
}

}
