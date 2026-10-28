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

    if (p_resize_epoch != _seen_resize_epoch) {
        retire_all();
        _seen_resize_epoch = p_resize_epoch;
    }
}

VkDescriptorSet ImGuiTextureCache::get(VkImageView p_view, VkSampler p_sampler)
{
    if (p_view == VK_NULL_HANDLE) return VK_NULL_HANDLE;

    const Key key{ p_view, p_sampler ? p_sampler : sampler };
    auto it = _entries.find(key);
    if (it != _entries.end()) {
        it->second.last_used_frame = frame_number;
        return it->second.set;
    }

    VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(key.sampler, p_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    _entries.emplace(key, Entry{ set, frame_number });
    return set;
}

void ImGuiTextureCache::retire(VkImageView p_view, uint64_t p_frame_number, uint32_t p_frame_count)
{
    if (p_view == VK_NULL_HANDLE) return;
    for (auto it = _entries.begin(); it != _entries.end();) {
        if (it->first.view == p_view) {
            _retired.push_back({ it->second.set, p_frame_number + p_frame_count });
            it = _entries.erase(it);
        } else {
            ++it;
        }
    }
}

void ImGuiTextureCache::retire_all()
{
    for (auto& [view, e] : _entries) _retired.push_back({ e.set, frame_number + frame_count });
    _entries.clear();
}

void ImGuiTextureCache::collect(uint64_t p_frame_number)
{
    for (auto it = _entries.begin(); it != _entries.end();) {
        if (p_frame_number > it->second.last_used_frame + frame_count) {
            _retired.push_back({ it->second.set, p_frame_number + frame_count });
            it = _entries.erase(it);
        } else {
            ++it;
        }
    }

    for (size_t i = 0; i < _retired.size();) {
        if (p_frame_number >= _retired[i].retire_frame) {
            ImGui_ImplVulkan_RemoveTexture(_retired[i].set);
            _retired[i] = _retired.back();
            _retired.pop_back();
        } else {
            ++i;
        }
    }
}

void ImGuiTextureCache::_invalidate_all()
{
    for (Retired& r : _retired) ImGui_ImplVulkan_RemoveTexture(r.set);
    for (auto& [view, e] : _entries) ImGui_ImplVulkan_RemoveTexture(e.set);
    _retired.clear();
    _entries.clear();
}

}
