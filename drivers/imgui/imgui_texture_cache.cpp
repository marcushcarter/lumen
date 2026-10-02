#include <drivers/imgui/imgui_texture_cache.h>
#include <backends/imgui_impl_vulkan.h>

namespace lumen::drivers {

Error ImGuiTextureCache::initialize(VkSampler p_sampler)
{
    using enum Error;
    sampler = p_sampler;
    return OK;
}

void ImGuiTextureCache::shutdown()
{
    _invalidate_all();
}

void ImGuiTextureCache::begin_frame(uint64_t p_frame_number, uint32_t p_frame_count, uint64_t p_resize_epoch)
{
    frame_number = p_frame_number;
    frame_count = p_frame_count;

    if (p_resize_epoch != seen_resize_epoch) {
        retire_all();
        seen_resize_epoch = p_resize_epoch;
    }
}

VkDescriptorSet ImGuiTextureCache::get(VkImageView p_view)
{
    if (p_view == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    auto it = entries.find(p_view);
    if (it != entries.end()) {
        it->second.last_used_frame = frame_number;
        return it->second.set;
    }

    VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(sampler, p_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    entries.emplace(p_view, Entry{ set, frame_number });
    return set;
}

void ImGuiTextureCache::retire(VkImageView p_view, uint64_t p_frame_number, uint32_t p_frame_count)
{
    if (p_view == VK_NULL_HANDLE) return;
    auto it = entries.find(p_view);
    if (it == entries.end()) return;
    retired.push_back({ it->second.set, p_frame_number + p_frame_count });
    entries.erase(it);
}

void ImGuiTextureCache::retire_all()
{
    for (auto& [view, e] : entries) retired.push_back({ e.set, frame_number + frame_count });
    entries.clear();
}

void ImGuiTextureCache::collect(uint64_t p_frame_number)
{
    for (auto it = entries.begin(); it != entries.end();) {
        if (p_frame_number > it->second.last_used_frame + frame_count) {
            retired.push_back({ it->second.set, p_frame_number + frame_count });
            it = entries.erase(it);
        } else {
            ++it;
        }
    }

    for (size_t i = 0; i < retired.size();) {
        if (p_frame_number >= retired[i].retire_frame) {
            ImGui_ImplVulkan_RemoveTexture(retired[i].set);
            retired[i] = retired.back();
            retired.pop_back();
        } else {
            ++i;
        }
    }
}

void ImGuiTextureCache::_invalidate_all()
{
    for (Retired& r : retired) ImGui_ImplVulkan_RemoveTexture(r.set);
    for (auto& [view, e] : entries) ImGui_ImplVulkan_RemoveTexture(e.set);
    retired.clear();
    entries.clear();
}

}