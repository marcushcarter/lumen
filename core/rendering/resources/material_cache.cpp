#include <core/rendering/resources/material_cache.h>
#include <core/rendering/resources/texture_cache.h>

namespace lumen {

using Buffer = drivers::DeviceDriverVulkan::Buffer;

Error MaterialCache::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;
    dd = &r_dd;
    frame_count = p_frame_count ? p_frame_count : 1;
    buffers.resize(frame_count);
    _uploaded_version.assign(frame_count, UINT64_MAX);
    gpu.assign(1, default_material());
    return _grow(INITIAL_CAPACITY);
}

void MaterialCache::shutdown()
{
    for (Buffer& b : buffers) dd->buffer_free(b);
    for (Retired& r : _retired) dd->buffer_free(r.buffer);
    buffers.clear();
    _retired.clear();
    _uploaded_version.clear();
    by_guid.clear();
    gpu.clear();
    _free.clear();
    capacity = 0;
}

Error MaterialCache::load(const std::filesystem::path& p_path)
{
    using enum Error;
    LMaterial m;
    if (m.load(p_path) != OK || m.guid == Guid{}) return FAILED;

    auto it = by_guid.find(m.guid);
    if (it != by_guid.end()) {
        it->second.material = m;
    } else {
        uint32_t index;
        if (!_free.empty()) {
            index = _free.back();
            _free.pop_back();
        } else {
            index = (uint32_t)gpu.size();
            gpu.push_back(default_material());
        }
        by_guid.emplace(m.guid, Entry{ m, index });
        layout_version++;
    }
    _content_version++;
    return OK;
}

void MaterialCache::unload(Guid p_guid)
{
    auto it = by_guid.find(p_guid);
    if (it == by_guid.end()) return;
    _free.push_back(it->second.index);
    by_guid.erase(it);
    layout_version++;
    _content_version++;
}

void MaterialCache::clear()
{
    by_guid.clear();
    _free.clear();
    gpu.assign(1, default_material());
    layout_version++;
    _content_version++;
}

void MaterialCache::begin_frame(uint64_t p_frame_number)
{
    frame_number = p_frame_number;
    for (size_t i = 0; i < _retired.size();) {
        if (frame_number >= _retired[i].frame + frame_count) {
            dd->buffer_free(_retired[i].buffer);
            _retired[i] = std::move(_retired.back());
            _retired.pop_back();
        } else {
            ++i;
        }
    }
}

void MaterialCache::resolve(const TextureCache& p_textures)
{
    if (_built_content == _content_version && _built_textures == p_textures.version) return;

    gpu[MATERIAL_DEFAULT] = default_material();
    for (uint32_t i : _free) gpu[i] = default_material();
    for (const auto& [guid, entry] : by_guid) gpu[entry.index] = _build(entry.material, p_textures);

    _built_content = _content_version;
    _built_textures = p_textures.version;
    version++;
}

void MaterialCache::upload(uint32_t p_frame)
{
    if (p_frame >= buffers.size()) return;
    if (gpu.size() > capacity) {
        uint32_t next = capacity ? capacity : INITIAL_CAPACITY;
        while (next < gpu.size()) next *= 2;
        if (_grow(next) != Error::OK) return;
    }
    if (_uploaded_version[p_frame] == version) return;

    Buffer& b = buffers[p_frame];
    const VkDeviceSize bytes = (VkDeviceSize)gpu.size() * sizeof(GPUMaterial);
    dd->buffer_update(b, gpu.data(), bytes);
    dd->buffer_flush(b, 0, bytes);
    _uploaded_version[p_frame] = version;
}

Error MaterialCache::_grow(uint32_t p_capacity)
{
    using enum Error;
    for (Buffer& b : buffers) {
        if (b.buffer) _retired.push_back({ frame_number, std::move(b) });
        b = dd->buffer_create({ .size = (VkDeviceSize)p_capacity * sizeof(GPUMaterial), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "materials" });
        LUMEN_ERR_FAIL_COND_V_MSG(!b.buffer, FAILED, "MaterialCache: material buffer allocation failed.");
    }
    capacity = p_capacity;
    _uploaded_version.assign(buffers.size(), UINT64_MAX);
    return OK;
}

GPUMaterial MaterialCache::default_material()
{
    GPUMaterial g{};
    g.base_color = vec4(1.0f);
    g.emissive = vec3(0.0f);
    g.alpha_cutoff = 0.5f;
    g.custom = vec4(0.0f);
    g.uv_transform = vec4(1.0f, 1.0f, 0.0f, 0.0f);
    g.roughness = 0.5f;
    g.metallic = 0.0f;
    g.normal_scale = 1.0f;
    g.ao_strength = 1.0f;
    g.base_color_tex = TEXTURE_NONE;
    g.normal_tex = TEXTURE_NONE;
    g.roughness_tex = TEXTURE_NONE;
    g.metallic_tex = TEXTURE_NONE;
    g.ao_tex = TEXTURE_NONE;
    g.emissive_tex = TEXTURE_NONE;
    g.custom_tex = TEXTURE_NONE;
    g.flags = (uint32_t)ShadingModel::DEFAULT_LIT;
    return g;
}

GPUMaterial MaterialCache::_build(const LMaterial& p_material, const TextureCache& p_textures)
{
    const LMaterial& m = p_material;
    GPUMaterial g{};
    g.base_color = m.base_color;
    g.emissive = m.emissive_color * m.emissive_nits;
    g.alpha_cutoff = m.alpha_cutoff;
    g.custom = m.custom;
    g.uv_transform = vec4(m.uv_scale, m.uv_offset);
    g.roughness = m.roughness;
    g.metallic = m.metallic;
    g.normal_scale = m.normal_scale;
    g.ao_strength = m.ao_strength;
    g.base_color_tex = p_textures.bindless_of(m.base_color_texture);
    g.normal_tex = p_textures.bindless_of(m.normal_texture);
    g.roughness_tex = p_textures.bindless_of(m.roughness_texture);
    g.metallic_tex = p_textures.bindless_of(m.metallic_texture);
    g.ao_tex = p_textures.bindless_of(m.ao_texture);
    g.emissive_tex = p_textures.bindless_of(m.emissive_texture);
    g.custom_tex = p_textures.bindless_of(m.custom_texture);
    g.flags = (uint32_t)m.shading_model | ((uint32_t)m.alpha_mode << MATERIAL_ALPHA_SHIFT) | (m.two_sided ? MATERIAL_FLAG_TWO_SIDED : 0u);
    return g;
}

}
