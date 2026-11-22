#include <core/rendering/resources/mesh_slot_cache.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/rendering/resources/material_cache.h>
#include <core/assets/lmesh.h>

namespace lumen {

using Buffer = drivers::DeviceDriverVulkan::Buffer;

Error MeshSlotCache::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;
    dd = &r_dd;
    const uint32_t frame_count = p_frame_count ? p_frame_count : 1;
    slot_buffers.resize(frame_count);
    _uploaded_version.assign(frame_count, UINT64_MAX);
    for (Buffer& b : slot_buffers) {
        b = dd->buffer_create({ .size = (VkDeviceSize)MAX_SLOTS * sizeof(uint32_t), .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .device_local = false, .host_visible = true, .pool = dd->bar_pool(), .name = "model_slot_table" });
        LUMEN_ERR_FAIL_COND_V_MSG(!b.buffer, FAILED, "MeshSlotCache: slot table allocation failed.");
    }
    slot_table.assign(1, DEFAULT_MATERIAL);
    return OK;
}

void MeshSlotCache::shutdown()
{
    for (Buffer& b : slot_buffers) dd->buffer_free(b);
    slot_buffers.clear();
    _uploaded_version.clear();
    by_guid.clear();
    slot_table.clear();
}

Error MeshSlotCache::load(const std::filesystem::path& p_lmesh)
{
    Guid guid;
    std::vector<Guid> materials;
    if (!lmesh_asset_materials(p_lmesh, guid, materials)) return Error::FAILED;
    set(guid, guid, materials);
    return Error::OK;
}

void MeshSlotCache::unload(Guid p_guid)
{
    if (!by_guid.erase(p_guid)) return;
    _geometry_version = UINT64_MAX;
    version++;
}

void MeshSlotCache::set(Guid p_guid, Guid p_mesh, const std::vector<Guid>& p_materials)
{
    if (auto it = by_guid.find(p_guid); it != by_guid.end() && it->second.mesh == p_mesh) {
        if (it->second.materials != p_materials) {
            it->second.materials = p_materials;
            _slots_dirty = true;
        }
        return;
    }
    Entry& e = by_guid[p_guid];
    e = Entry{};
    e.mesh = p_mesh;
    e.materials = p_materials;
    _geometry_version = UINT64_MAX;
    version++;
}

void MeshSlotCache::clear()
{
    by_guid.clear();
    _geometry_version = UINT64_MAX;
    version++;
}

void MeshSlotCache::_write_slots(const Entry& p_entry, const MaterialCache& p_materials)
{
    for (uint32_t s = 0; s < p_entry.slot_count; ++s) {
        const uint32_t index = s < p_entry.materials.size() ? p_materials.index_of(p_entry.materials[s]) : DEFAULT_MATERIAL;
        slot_table[p_entry.slot_base + s] = (index & SLOT_MATERIAL_MASK) | (p_materials.bin_of(index) << SLOT_BIN_SHIFT);
    }
}

void MeshSlotCache::resolve(const GeometryPool& p_geometry, const MaterialCache& p_materials)
{
    if (_geometry_version == p_geometry.version && _material_layout == p_materials.layout_version) {
        if (!_slots_dirty && _material_bins == p_materials.bin_version) return;
        for (const auto& [guid, entry] : by_guid) _write_slots(entry, p_materials);
        _slots_dirty = false;
        _material_bins = p_materials.bin_version;
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
    if (overflow) log_write("MeshSlotCache: slot table full (%u), some meshes use the default material.", MAX_SLOTS);

    _geometry_version = p_geometry.version;
    _material_layout = p_materials.layout_version;
    _material_bins = p_materials.bin_version;
    _slots_dirty = false;
    version++;
    slot_version++;
}

void MeshSlotCache::upload(uint32_t p_frame)
{
    if (p_frame >= slot_buffers.size() || _uploaded_version[p_frame] == slot_version) return;
    Buffer& b = slot_buffers[p_frame];
    const VkDeviceSize bytes = (VkDeviceSize)slot_table.size() * sizeof(uint32_t);
    dd->buffer_update(b, slot_table.data(), bytes);
    dd->buffer_flush(b, 0, bytes);
    _uploaded_version[p_frame] = slot_version;
}

}
