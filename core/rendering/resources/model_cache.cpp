#include <core/rendering/resources/model_cache.h>
#include <core/rendering/resources/geometry_pool.h>
#include <core/assets/lmodel.h>

namespace lumen {

using Buffer = drivers::DeviceDriverVulkan::Buffer;

Error ModelCache::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;
    dd = &r_dd;
    const uint32_t frame_count = p_frame_count ? p_frame_count : 1;
    slot_buffers.resize(frame_count);
    _uploaded_version.assign(frame_count, UINT64_MAX);
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
    _uploaded_version.clear();
    by_guid.clear();
    slot_table.clear();
}

Error ModelCache::load(const std::filesystem::path& p_path)
{
    using enum Error;
    LModel model;
    if (model.load(p_path) != OK || model.guid == Guid{}) return FAILED;
    by_guid[model.guid] = Entry{ model.mesh };
    _geometry_version = UINT64_MAX;
    version++;
    return OK;
}

void ModelCache::set(Guid p_guid, Guid p_mesh)
{
    by_guid[p_guid] = Entry{ p_mesh };
    _geometry_version = UINT64_MAX;
    version++;
}

void ModelCache::unload(Guid p_guid)
{
    if (!by_guid.erase(p_guid)) return;
    _geometry_version = UINT64_MAX;
    version++;
}

void ModelCache::clear()
{
    by_guid.clear();
    _geometry_version = UINT64_MAX;
    version++;
}

void ModelCache::resolve(const GeometryPool& p_geometry)
{
    if (_geometry_version == p_geometry.version) return;

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
        slot_table.insert(slot_table.end(), mesh->slot_count, DEFAULT_MATERIAL);
    }
    if (overflow) log_write("ModelCache: slot table full (%u), some models use the default material.", MAX_SLOTS);

    _geometry_version = p_geometry.version;
    version++;
}

void ModelCache::upload(uint32_t p_frame)
{
    if (p_frame >= slot_buffers.size() || _uploaded_version[p_frame] == version) return;
    Buffer& b = slot_buffers[p_frame];
    const VkDeviceSize bytes = (VkDeviceSize)slot_table.size() * sizeof(uint32_t);
    dd->buffer_update(b, slot_table.data(), bytes);
    dd->buffer_flush(b, 0, bytes);
    _uploaded_version[p_frame] = version;
}

}