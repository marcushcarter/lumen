#include <editor/assets/texture_viewer.h>
#include <editor/assets/asset_registry.h>
#include <editor/application/editor_context.h>
#include <drivers/imgui/imgui_driver.h>
#include <editor/ui/ui_style.h>
#include <core/rendering/renderer.h>
#include <vulkan/vk_enum_string_helper.h>
#include <IconsFontAwesome6.h>
#include <algorithm>
#include <cmath>
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

static bool _channel_valid(TextureViewer::Channel p_channel, uint32_t p_channels)
{
    using enum TextureViewer::Channel;
    switch (p_channel) {
        case RGBA: return p_channels == 4;
        case RGB: return true;
        case R: return true;
        case G: return p_channels >= 2;
        case B: return p_channels >= 3;
        case A: return p_channels == 4;
    }
    return false;
}

VkComponentMapping TextureViewer::swizzle(Channel p_channel, uint32_t p_channels)
{
    const VkComponentSwizzle r = VK_COMPONENT_SWIZZLE_R;
    const VkComponentSwizzle g = VK_COMPONENT_SWIZZLE_G;
    const VkComponentSwizzle b = VK_COMPONENT_SWIZZLE_B;
    const VkComponentSwizzle a = VK_COMPONENT_SWIZZLE_A;
    const VkComponentSwizzle one = VK_COMPONENT_SWIZZLE_ONE;
    switch (p_channel) {
        case TextureViewer::Channel::RGBA:
            if (p_channels == 4) return { r, g, b, a };
            [[fallthrough]];
        case TextureViewer::Channel::RGB:
            if (p_channels == 1) return { r, r, r, one };
            if (p_channels == 2) return { r, g, one, one };
            return { r, g, b, one };
        case TextureViewer::Channel::R: return { r, r, r, one };
        case TextureViewer::Channel::G: return { g, g, g, one };
        case TextureViewer::Channel::B: return { b, b, b, one };
        case TextureViewer::Channel::A: return { a, a, a, one };
    }
    return { r, g, b, a };
}

static void _format_name(VkFormat p_format, char* r_out, size_t p_size)
{
    const char* name = string_VkFormat(p_format);
    if (std::strncmp(name, "VK_FORMAT_", 10) == 0) name += 10;
    std::snprintf(r_out, p_size, "%s", name);
    const size_t len = std::strlen(r_out);
    if (len > 6 && std::strcmp(r_out + len - 6, "_BLOCK") == 0) r_out[len - 6] = '\0';
}

static void _mip_label(const Image& p_image, int32_t p_mip, char* r_out, size_t p_size)
{
    if (p_mip < 0) {
        std::snprintf(r_out, p_size, "Mip: Auto");
        return;
    }
    const uint32_t w = std::max(1u, p_image.extent.width >> p_mip);
    const uint32_t h = std::max(1u, p_image.extent.height >> p_mip);
    std::snprintf(r_out, p_size, "Mip %d  (%ux%u)", p_mip, w, h);
}

static void _draw_checker(ImDrawList* p_dl, ImVec2 p_min, ImVec2 p_max, ImVec2 p_clip_min, ImVec2 p_clip_max)
{
    const ImVec2 v0(std::max(p_min.x, p_clip_min.x), std::max(p_min.y, p_clip_min.y));
    const ImVec2 v1(std::min(p_max.x, p_clip_max.x), std::min(p_max.y, p_clip_max.y));
    if (v0.x >= v1.x || v0.y >= v1.y) return;

    const float s = TextureViewer::CHECKER_SIZE;
    p_dl->AddRectFilled(v0, v1, IM_COL32(58, 58, 58, 255));
    const int x0 = (int)std::floor((v0.x - p_min.x) / s);
    const int y0 = (int)std::floor((v0.y - p_min.y) / s);
    const int x1 = (int)std::ceil((v1.x - p_min.x) / s);
    const int y1 = (int)std::ceil((v1.y - p_min.y) / s);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0 + ((x0 + y) & 1 ? 0 : 1); x < x1; x += 2) {
            const ImVec2 a(std::max(p_min.x + x * s, v0.x), std::max(p_min.y + y * s, v0.y));
            const ImVec2 b(std::min(p_min.x + (x + 1) * s, v1.x), std::min(p_min.y + (y + 1) * s, v1.y));
            p_dl->AddRectFilled(a, b, IM_COL32(84, 84, 84, 255));
        }
    }
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
    for (const View& v : _views) vkDestroyImageView(dd->device, v.view, nullptr);
    for (const Retired& r : _retired) vkDestroyImageView(dd->device, r.view, nullptr);
    _views.clear();
    _retired.clear();
    dd->sampler_free(sampler_linear);
    dd->sampler_free(sampler_nearest);
    dd = nullptr;
}

void TextureViewer::open(Guid p_guid)
{
    if (!is_open || guid != p_guid) {
        channel = Channel::RGBA;
        mip = -1;
        _fit_pending = true;
    }
    guid = p_guid;
    is_open = true;
    _focus_pending = true;
}

void TextureViewer::close(EditorContext& ctx)
{
    _release_views(ctx);
    is_open = false;
    guid = {};
}

void TextureViewer::draw(EditorContext& ctx)
{
    _collect(ctx.renderer->frame_number, ctx.renderer->frame_count);
    if (!is_open) return;

    const AssetRegistry::Entry* entry = ctx.assets->find(guid);
    if (!entry && !ctx.assets->dirty) {
        close(ctx);
        return;
    }

    const LTexture* tex = ctx.renderer->textures.get(guid);
    const VkImage image = tex ? tex->image.image : VK_NULL_HANDLE;
    if (image != _image) {
        _release_views(ctx);
        _image = image;
    }

    char title[256];
    std::snprintf(title, sizeof(title), "%s  %s###texture_viewer", ICON_FA_IMAGE, entry ? entry->name.c_str() : "Texture");
    ImGui::SetNextWindowSize(ImVec2(760.0f, 600.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    if (_focus_pending) {
        ImGui::SetNextWindowFocus();
        _focus_pending = false;
    }

    bool keep = true;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin(title, &keep, flags)) {
        if (tex) _draw_contents(ctx, tex->image);
        else ImGui::TextDisabled("Texture is not loaded.");
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) keep = false;
    }
    ImGui::End();
    if (!keep) close(ctx);
}

void TextureViewer::_draw_contents(EditorContext& ctx, const Image& p_image)
{
    const ImGuiIO& io = ImGui::GetIO();
    const uint32_t channels = TextureViewer::channel_count(p_image.format);
    if (!_channel_valid(channel, channels)) channel = channels == 4 ? Channel::RGBA : Channel::RGB;
    if (mip >= (int32_t)p_image.mip_levels) mip = -1;

    auto channel_button = [&](const char* p_label, Channel p_channel) {
        const bool on = channel == p_channel;
        ImGui::BeginDisabled(!_channel_valid(p_channel, channels));
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(p_label)) channel = p_channel;
        if (on) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        ImGui::SameLine();
    };
    channel_button("RGBA", Channel::RGBA);
    channel_button("RGB", Channel::RGB);
    channel_button("R", Channel::R);
    channel_button("G", Channel::G);
    channel_button("B", Channel::B);
    channel_button("A", Channel::A);

    char label[64];
    _mip_label(p_image, mip, label, sizeof(label));
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::BeginCombo("##mip", label)) {
        for (int32_t m = -1; m < (int32_t)p_image.mip_levels; ++m) {
            _mip_label(p_image, m, label, sizeof(label));
            if (ImGui::Selectable(label, m == mip)) mip = m;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Fit")) _fit_pending = true;
    ImGui::SameLine();
    if (ImGui::Button("1:1")) {
        zoom = 1.0f;
        pan = ImVec2(0.0f, 0.0f);
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%.0f%%", zoom * 100.0f);

    const float w = (float)p_image.extent.width;
    const float h = (float)p_image.extent.height;
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    canvas_size.y -= ImGui::GetTextLineHeightWithSpacing();
    if (canvas_size.x < 1.0f || canvas_size.y < 1.0f || w < 1.0f || h < 1.0f) return;
    const ImVec2 canvas_max(canvas_min.x + canvas_size.x, canvas_min.y + canvas_size.y);
    const ImVec2 canvas_center(canvas_min.x + canvas_size.x * 0.5f, canvas_min.y + canvas_size.y * 0.5f);

    ImGui::InvisibleButton("##canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActive()) {
        pan.x += io.MouseDelta.x;
        pan.y += io.MouseDelta.y;
    }

    if (_fit_pending) {
        zoom = std::clamp(std::min(canvas_size.x / w, canvas_size.y / h), MIN_ZOOM, MAX_ZOOM);
        pan = ImVec2(0.0f, 0.0f);
        _fit_pending = false;
    }

    if (hovered && io.MouseWheel != 0.0f) {
        const float next = std::clamp(zoom * std::pow(ZOOM_STEP, io.MouseWheel), MIN_ZOOM, MAX_ZOOM);
        const ImVec2 rel((io.MousePos.x - canvas_center.x - pan.x) / zoom, (io.MousePos.y - canvas_center.y - pan.y) / zoom);
        pan.x = io.MousePos.x - canvas_center.x - rel.x * next;
        pan.y = io.MousePos.y - canvas_center.y - rel.y * next;
        zoom = next;
    }

    const ImVec2 p0(std::floor(canvas_center.x + pan.x - w * zoom * 0.5f), std::floor(canvas_center.y + pan.y - h * zoom * 0.5f));
    const ImVec2 p1(p0.x + w * zoom, p0.y + h * zoom);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvas_min, canvas_max, true);
    dl->AddRectFilled(canvas_min, canvas_max, ui_style().color.sunken);
    if (channel == Channel::RGBA) _draw_checker(dl, p0, p1, canvas_min, canvas_max);

    const VkSampler sampler = zoom >= 1.0f ? sampler_nearest.sampler : sampler_linear.sampler;
    const VkDescriptorSet set = ctx.imgui->texture_cache.get(_view(p_image, mip, channel), sampler);
    if (set) dl->AddImage((ImTextureID)set, p0, p1);
    dl->AddRect(ImVec2(p0.x - 1.0f, p0.y - 1.0f), ImVec2(p1.x + 1.0f, p1.y + 1.0f), ui_style().color.line);
    dl->PopClipRect();

    char format[64];
    _format_name(p_image.format, format, sizeof(format));
    ImGui::TextDisabled("%ux%u  %s  %u mips  %.2f MB", p_image.extent.width, p_image.extent.height, format, p_image.mip_levels, (double)p_image.mem_req.size / (1024.0 * 1024.0));
    if (hovered) {
        const int tx = (int)std::floor((io.MousePos.x - p0.x) / zoom);
        const int ty = (int)std::floor((io.MousePos.y - p0.y) / zoom);
        if (tx >= 0 && ty >= 0 && tx < (int)p_image.extent.width && ty < (int)p_image.extent.height) {
            ImGui::SameLine();
            ImGui::TextDisabled("  (%d, %d)", tx, ty);
        }
    }
}

VkImageView TextureViewer::_view(const Image& p_image, int32_t p_mip, Channel p_channel)
{
    for (const View& v : _views) {
        if (v.mip == p_mip && v.channel == p_channel) return v.view;
    }

    VkImageViewCreateInfo ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    ci.image = p_image.image;
    ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci.format = p_image.format;
    ci.components = swizzle(p_channel, TextureViewer::channel_count(p_image.format));
    ci.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, p_mip < 0 ? 0u : (uint32_t)p_mip, p_mip < 0 ? p_image.mip_levels : 1u, 0, 1 };
    VkImageView view = VK_NULL_HANDLE;
    if (vkCreateImageView(dd->device, &ci, nullptr, &view) != VK_SUCCESS) return VK_NULL_HANDLE;
    _views.push_back({ view, p_mip, p_channel });
    return view;
}

void TextureViewer::_release_views(EditorContext& ctx)
{
    for (const View& v : _views) {
        ctx.imgui->texture_cache.retire(v.view, ctx.renderer->frame_number, ctx.renderer->frame_count);
        _retired.push_back({ v.view, ctx.renderer->frame_number });
    }
    _views.clear();
    _image = VK_NULL_HANDLE;
}

void TextureViewer::_collect(uint64_t p_frame_number, uint32_t p_frame_count)
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
