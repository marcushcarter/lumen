#include <core/rendering/resources/model_cache.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/rendering/resources/material_cache.h>
#include <core/assets/lmodel.h>

namespace lumen {

using Buffer = drivers::DeviceDriverVulkan::Buffer;

Error ModelCache::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;
    dd = &r_dd;
    const uint32_t frame_count = p_frame_count ? p_frame_count : 1;
    slot_buffers.resize(frame_count);
    uploaded_version.assign(frame_count, UINT64_MAX);
    for (Buffer& b : slot_buffers) {
        b = dd->buffer_create({ .size = (VkDeviceSize)MAX_SLOTS * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "model_slot_table" });
        LUMEN_ERR_FAIL_COND_V_MSG(!b.buffer, FAILED, "ModelCache: slot table allocation failed.");
    }
    slot_table.assign(1, DEFAULT_MATERIAL);
    return OK;
}

void ModelCache::shutdown()
{
    for (Buffer& b : slot_buffers) dd->buffer_free(b);
    slot_buffers.clear();
    uploaded_version.clear();
    by_guid.clear();
    slot_table.clear();
}

Error ModelCache::load(const std::filesystem::path& p_path)
{
    using enum Error;
    LModel model;
    if (model.load(p_path) != OK || model.guid == Guid{}) return FAILED;
    Entry& e = by_guid[model.guid];
    e = Entry{};
    e.mesh = model.mesh;
    e.materials = model.materials;
    geometry_version = UINT64_MAX;
    version++;
    return OK;
}

void ModelCache::unload(Guid p_guid)
{
    if (!by_guid.erase(p_guid)) return;
    geometry_version = UINT64_MAX;
    version++;
}

void ModelCache::set(Guid p_guid, Guid p_mesh, const std::vector<Guid>& p_materials)
{
    if (auto it = by_guid.find(p_guid); it != by_guid.end() && it->second.mesh == p_mesh) {
        if (it->second.materials != p_materials) {
            it->second.materials = p_materials;
            slots_dirty = true;
        }
        return;
    }
    Entry& e = by_guid[p_guid];
    e = Entry{};
    e.mesh = p_mesh;
    e.materials = p_materials;
    geometry_version = UINT64_MAX;
    version++;
}

void ModelCache::clear()
{
    by_guid.clear();
    geometry_version = UINT64_MAX;
    version++;
}

void ModelCache::_write_slots(const Entry& p_entry, const MaterialCache& p_materials)
{
    for (uint32_t s = 0; s < p_entry.slot_count; ++s) slot_table[p_entry.slot_base + s] = s < p_entry.materials.size() ? p_materials.index_of(p_entry.materials[s]) : DEFAULT_MATERIAL;
}

void ModelCache::resolve(const GeometryPool& p_geometry, const MaterialCache& p_materials)
{
    if (geometry_version == p_geometry.version && material_layout == p_materials.layout_version) {
        if (!slots_dirty) return;
        for (const auto& [guid, entry] : by_guid) _write_slots(entry, p_materials);
        slots_dirty = false;
        slot_version++;
        return;
    }

    slot_table.assign(1, DEFAULT_MATERIAL);
    bool overflow = false;
    for (auto& [guid, entry] : by_guid) {
        entry.mesh_index = p_geometry.find(entry.mesh);
        entry.slot_base = 0;
        entry.slot_count = 0;
        const LMesh* mesh = p_geometry.get(entry.mesh_index);
        if (!mesh) continue;
        if (slot_table.size() + mesh->slot_count > MAX_SLOTS) {
            overflow = true;
            continue;
        }
        entry.slot_base = (uint32_t)slot_table.size();
        entry.slot_count = mesh->slot_count;
        slot_table.resize(slot_table.size() + mesh->slot_count, DEFAULT_MATERIAL);
        _write_slots(entry, p_materials);
    }
    if (overflow) log_write("ModelCache: slot table full (%u), some models use the default material.", MAX_SLOTS);

    geometry_version = p_geometry.version;
    material_layout = p_materials.layout_version;
    slots_dirty = false;
    version++;
    slot_version++;
}

void ModelCache::upload(uint32_t p_frame)
{
    if (p_frame >= slot_buffers.size() || uploaded_version[p_frame] == slot_version) return;
    Buffer& b = slot_buffers[p_frame];
    const VkDeviceSize bytes = (VkDeviceSize)slot_table.size() * sizeof(uint32_t);
    dd->buffer_update(b, slot_table.data(), bytes);
    dd->buffer_flush(b, 0, bytes);
    uploaded_version[p_frame] = slot_version;
}

}