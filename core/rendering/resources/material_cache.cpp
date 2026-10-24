#include <core/rendering/resources/material_cache.h>
#include <core/rendering/resources/texture_cache.h>

namespace lumen {

using Buffer = drivers::DeviceDriverVulkan::Buffer;

Error MaterialCache::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;
    dd = &r_dd;
    frame_count = p_frame_count ? p_frame_count : 1;
    slot_buffers.resize(frame_count);
    uploaded_version.assign(frame_count, UINT64_MAX);
    materials.assign(1, default_material());
    bins.assign(1, (uint8_t)RASTER_BIN_OPAQUE);
    return _grow(INITIAL_CAPACITY);
}

void MaterialCache::shutdown()
{
    for (Buffer& b : slot_buffers) dd->buffer_free(b);
    for (Retired& r : retired) dd->buffer_free(r.buffer);
    slot_buffers.clear();
    retired.clear();
    uploaded_version.clear();
    by_guid.clear();
    materials.clear();
    bins.clear();
    free_slots.clear();
    capacity = 0;
}

void MaterialCache::begin_frame(uint64_t p_frame_number)
{
    frame_number = p_frame_number;
    for (size_t i = 0; i < retired.size();) {
        if (frame_number >= retired[i].frame + frame_count) {
            dd->buffer_free(retired[i].buffer);
            retired[i] = std::move(retired.back());
            retired.pop_back();
        } else {
            ++i;
        }
    }
}

Error MaterialCache::load(const std::filesystem::path& p_path)
{
    using enum Error;
    LMaterial m;
    if (m.load(p_path) != OK) return FAILED;
    return set(m);
}

void MaterialCache::unload(Guid p_guid)
{
    auto it = by_guid.find(p_guid);
    if (it == by_guid.end()) return;
    free_slots.push_back(it->second.index);
    _set_bin(it->second.index, (uint8_t)RASTER_BIN_OPAQUE);
    by_guid.erase(it);
    layout_version++;
    content_version++;
}

Error MaterialCache::set(const LMaterial& p_material)
{
    using enum Error;
    const LMaterial& m = p_material;
    if (m.guid == Guid{}) return FAILED;

    auto it = by_guid.find(m.guid);
    if (it != by_guid.end()) {
        it->second.material = m;
        _set_bin(it->second.index, _bin(m));
    } else {
        uint32_t index;
        if (!free_slots.empty()) {
            index = free_slots.back();
            free_slots.pop_back();
        } else {
            index = (uint32_t)materials.size();
            materials.push_back(default_material());
            bins.push_back((uint8_t)RASTER_BIN_OPAQUE);
        }
        by_guid.emplace(m.guid, Entry{ m, index });
        _set_bin(index, _bin(m));
        layout_version++;
    }
    content_version++;
    return OK;
}

void MaterialCache::clear()
{
    by_guid.clear();
    free_slots.clear();
    materials.assign(1, default_material());
    bins.assign(1, (uint8_t)RASTER_BIN_OPAQUE);
    bin_version++;
    layout_version++;
    content_version++;
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

uint8_t MaterialCache::_bin(const LMaterial& p_material)
{
    const bool masked = p_material.alpha_mode == AlphaMode::MASKED && (p_material.base_color_texture != Guid{} || p_material.base_color.a < p_material.alpha_cutoff);
    return (uint8_t)((masked ? RASTER_BIN_MASKED : 0u) | (p_material.two_sided ? RASTER_BIN_TWO_SIDED : 0u));
}

void MaterialCache::_set_bin(uint32_t p_index, uint8_t p_bin)
{
    if (p_index >= bins.size() || bins[p_index] == p_bin) return;
    bins[p_index] = p_bin;
    bin_version++;
}

Error MaterialCache::_grow(uint32_t p_capacity)
{
    using enum Error;
    for (Buffer& b : slot_buffers) {
        if (b.buffer) retired.push_back({ frame_number, std::move(b) });
        b = dd->buffer_create({ .size = (VkDeviceSize)p_capacity * sizeof(GPUMaterial), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "materials" });
        LUMEN_ERR_FAIL_COND_V_MSG(!b.buffer, FAILED, "MaterialCache: material buffer allocation failed.");
    }
    capacity = p_capacity;
    uploaded_version.assign(slot_buffers.size(), UINT64_MAX);
    return OK;
}

void MaterialCache::resolve(const TextureCache& p_textures)
{
    if (built_content == content_version && built_textures == p_textures.version) return;

    materials[MATERIAL_DEFAULT] = default_material();
    for (uint32_t i : free_slots) materials[i] = default_material();
    for (const auto& [guid, entry] : by_guid) materials[entry.index] = _build(entry.material, p_textures);

    built_content = content_version;
    built_textures = p_textures.version;
    version++;
}

void MaterialCache::upload(uint32_t p_frame)
{
    if (p_frame >= slot_buffers.size()) return;
    if (materials.size() > capacity) {
        uint32_t next = capacity ? capacity : INITIAL_CAPACITY;
        while (next < materials.size()) next *= 2;
        if (_grow(next) != Error::OK) return;
    }
    if (uploaded_version[p_frame] == version) return;

    Buffer& b = slot_buffers[p_frame];
    const VkDeviceSize bytes = (VkDeviceSize)materials.size() * sizeof(GPUMaterial);
    dd->buffer_update(b, materials.data(), bytes);
    dd->buffer_flush(b, 0, bytes);
    uploaded_version[p_frame] = version;
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

}