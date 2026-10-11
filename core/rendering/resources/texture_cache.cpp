#include <core/rendering/resources/texture_cache.h>
#include <core/assets/asset_common.h>
#include <core/assets/ltexture.h>
#include <vulkan/vk_enum_string_helper.h>
#include <fstream>

namespace lumen {
    
Error TextureCache::initialize(drivers::DeviceDriverVulkan& r_dd)
{
    using enum Error;
    dd = &r_dd;
    return OK;
}

void TextureCache::begin_frame(uint64_t p_frame_number, uint32_t p_frame_count)
{
    frame_number = p_frame_number;
    frame_count = p_frame_count;
    for (size_t i = 0; i < retired.size();) {
        if (frame_number >= retired[i].frame + frame_count) {
            dd->image_free(retired[i].image);
            retired[i] = std::move(retired.back());
            retired.pop_back();
        } else {
            ++i;
        }
    }
}

uint32_t TextureCache::load(Guid p_guid, const std::filesystem::path& p_path)
{
    if (auto it = by_guid.find(p_guid); it != by_guid.end()) return it->second;

    std::ifstream f(p_path, std::ios::binary | std::ios::ate);
    if (!f) { log_write("TextureCache: cannot open %s", p_path.string().c_str()); return UINT32_MAX; }

    const std::streamsize file_size = f.tellg();
    f.seekg(0);
    if (file_size < (std::streamsize)(sizeof(LAssetHeader) + sizeof(LTexturePayloadHeader))) {
        log_write("TextureCache: %s too small", p_path.string().c_str());
        return UINT32_MAX;
    }

    std::vector<uint8_t> bytes((size_t)file_size);
    f.read(reinterpret_cast<char*>(bytes.data()), file_size);
    if (!f) { log_write("TextureCache: read failed %s", p_path.string().c_str()); return UINT32_MAX; }

    LAssetHeader ah{};
    std::memcpy(&ah, bytes.data(), sizeof(ah));
    if (ah.magic != BCON_MAGIC || ah.version != LASSET_VERSION || ah.type != AssetType::TEXTURE) {
        log_write("TextureCache: bad asset header %s", p_path.string().c_str());
        return UINT32_MAX;
    }

    LTexturePayloadHeader ph{};
    std::memcpy(&ph, bytes.data() + sizeof(ah), sizeof(ph));

    if (ah.payload_size < sizeof(ph)) {
        log_write("TextureCache: payload_size underflow %s", p_path.string().c_str());
        return UINT32_MAX;
    }

    const size_t payload_off = sizeof(ah) + sizeof(ph);
    const size_t blocks_size = (size_t)ah.payload_size - sizeof(ph);
    if (payload_off + blocks_size > bytes.size()) {
        log_write("TextureCache: truncated payload %s", p_path.string().c_str());
        return UINT32_MAX;
    }

    drivers::DeviceDriverVulkan::Image img = dd->image_create_texture_compressed((VkFormat)ph.vk_format, ph.width, ph.height, ph.mip_count, bytes.data() + payload_off, (VkDeviceSize)blocks_size, "ltexture");
    if (img.image == VK_NULL_HANDLE) {
        log_write("TextureCache: upload failed %s", p_path.string().c_str());
        return UINT32_MAX;
    }

    uint32_t slot;
    if (!free_slots.empty()) {
        slot = free_slots.back(); free_slots.pop_back();
        slots[slot] = LTexture{ p_guid, img };
    } else {
        slot = (uint32_t)slots.size();
        slots.push_back(LTexture{ p_guid, img });
    }
    by_guid.emplace(p_guid, slot);

    log_write("TextureCache: loaded %s %ux%u %s (mips=%u) slot=%u bindless=%u", p_path.string().c_str(), img.extent.width, img.extent.height, string_VkFormat(img.format), img.mip_levels, slot, img.bindless_sampled);
    return slot;
}

void TextureCache::unload(Guid p_guid)
{
    auto it = by_guid.find(p_guid);
    if (it == by_guid.end()) return;
    const uint32_t slot = it->second;
    retired.push_back({ frame_number, std::move(slots[slot].image) });
    slots[slot] = LTexture{};
    free_slots.push_back(slot);
    by_guid.erase(it);
}

void TextureCache::clear()
{
    for (auto& kv : by_guid) dd->image_free(slots[kv.second].image);
    for (Retired& r : retired) dd->image_free(r.image);
    retired.clear();
    slots.clear();
    free_slots.clear();
    by_guid.clear();
}

}