#include <core/rendering/features/water/water_feature.h>
#include <core/rendering/frame_data.h>
#include <core/io/embedded_resource.h>
#include <core/water/water_grid.h>
#include <algorithm>
#include <cstring>
#include <glm/gtc/packing.hpp>

namespace lumen {

using namespace water;

struct WaterLevelInfo
{
    VkDeviceAddress state, state16, stats, page, awake, init, surface, sources, events, pool, masks, bounds, gather, readback;
    int32_t origin_x, origin_y, slots, tiles_w, n;
    uint32_t cells, awake_count, init_count, surface_count, source_count, event_count, gather_count;
    float dx, gravity, manning, vorticity, vortex_min_depth, cfl_limit, flux_depth_cap, foam_decay, foam_gain;
};
static_assert(sizeof(WaterLevelInfo) == 200);

struct WaterItemGpu
{
    VkDeviceAddress samples, masks, bounds;
    float origin_x, origin_y, spacing, ymin, ymax;
    uint32_t has_bounds;
};
static_assert(sizeof(WaterItemGpu) == 48);

struct WaterSimPush
{
    VkDeviceAddress level;
    uint32_t pass;
    float dt;
    uint32_t flags;
    uint32_t pad;
};

struct WaterCullPush
{
    VkDeviceAddress camera, items, args, list;
    uint32_t count, capacity;
    float lod_error, min_quad;
    float screen[2];
    uint32_t hiz_index, hiz_mips, hiz_enabled;
};

struct WaterDrawPush
{
    VkDeviceAddress camera, items, list, light;
    uint32_t capacity;
    float lod_error;
    float time;
    uint32_t backdrop_index, width, height;
    uint32_t flags;
    uint32_t debug_mode;
};

struct WaterBackdropPush
{
    VkDeviceAddress camera;
    uint32_t color_index, depth_index, out_slot, width, height;
};

struct WaterDrawArgs
{
    uint32_t vertex_count, instance_count, first_vertex, first_instance;
};

enum : uint32_t { P_INIT, P_EVENTS, P_GHOST, P_ADVECT, P_CURLFORCE, P_FLUXLIMIT, P_HEIGHT, P_VELOCITY, P_SOURCES, P_FOAM, P_GATHER, P_DRAIN };
static constexpr uint32_t FLAG_VORTEX = 1;

static constexpr uint32_t BLOCKS = 16;
static constexpr VkDeviceSize TILE_BYTES = TILE_CELLS * sizeof(float);
static constexpr VkDeviceSize SAMPLE_BYTES = SURFACE_SAMPLES * sizeof(vec4);
static constexpr VkDeviceSize MASK_BYTES = SURFACE_MASK_WORDS * sizeof(uint32_t);
static constexpr VkDeviceSize READBACK_ENTRY = WaterGpuResult::ARRAYS * TILE_BYTES;

static void _barrier(VkCommandBuffer p_cmd, VkPipelineStageFlags2 p_src, VkAccessFlags2 p_src_access, VkPipelineStageFlags2 p_dst, VkAccessFlags2 p_dst_access)
{
    VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    mb.srcStageMask = p_src;
    mb.srcAccessMask = p_src_access;
    mb.dstStageMask = p_dst;
    mb.dstAccessMask = p_dst_access;
    VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &mb;
    vkCmdPipelineBarrier2(p_cmd, &dep);
}

static void _compute_barrier(VkCommandBuffer p_cmd)
{
    _barrier(p_cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

static void _all_barrier(VkCommandBuffer p_cmd)
{
    const VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    const VkAccessFlags2 access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
    _barrier(p_cmd, stages, access, stages, access);
}

WaterFeature::Buffer WaterFeature::_device_buffer(VkDeviceSize p_size, VkBufferUsageFlags p_usage, const char* p_name)
{
    drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
    ci.size = std::max<VkDeviceSize>(p_size, 16);
    ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | p_usage;
    ci.device_local = true;
    ci.name = p_name;
    return ctx->dd->buffer_create(ci);
}

void WaterFeature::_retire(Buffer& r_buffer)
{
    if (r_buffer.buffer) retired.push_back({ r_buffer, frame_counter });
    r_buffer = {};
}

void WaterFeature::_free_retired(bool p_all)
{
    const uint64_t keep = (uint64_t)ctx->graph->frame_count + 1;
    for (auto it = retired.begin(); it != retired.end();) {
        if (p_all || frame_counter - it->frame > keep) {
            ctx->dd->buffer_free(it->buffer);
            it = retired.erase(it);
        } else {
            ++it;
        }
    }
}

void WaterFeature::_reset_sim()
{
    _retire(state);
    _retire(state16);
    _retire(stats);
    _retire(pool);
    _retire(masks);
    _retire(bounds);
    capacity = 0;
}

void WaterFeature::_ensure_sim(VkCommandBuffer p_cmd, const WaterGpuLink& p_link)
{
    if (p_link.layout != layout) {
        _reset_sim();
        layout = p_link.layout;
    }
    if (p_link.capacity <= capacity) return;
    const uint32_t cap = p_link.capacity;
    const VkDeviceSize old_cells = (VkDeviceSize)capacity * TILE_CELLS;
    const VkDeviceSize new_cells = (VkDeviceSize)cap * TILE_CELLS;
    Buffer wide = _device_buffer(new_cells * WIDE_COUNT * sizeof(float), 0, "WaterState");
    Buffer half = _device_buffer(new_cells * HALF_COUNT * sizeof(uint16_t), 0, "WaterState16");
    Buffer st = _device_buffer((VkDeviceSize)cap * sizeof(uint32_t), 0, "WaterStats");
    vkCmdFillBuffer(p_cmd, st.buffer, 0, VK_WHOLE_SIZE, 0u);
    if (state.buffer && old_cells) {
        std::vector<VkBufferCopy> regions;
        for (uint32_t a = 0; a < WIDE_COUNT; a++) regions.push_back({ a * old_cells * sizeof(float), a * new_cells * sizeof(float), old_cells * sizeof(float) });
        vkCmdCopyBuffer(p_cmd, state.buffer, wide.buffer, (uint32_t)regions.size(), regions.data());
        regions.clear();
        for (uint32_t a = 0; a < HALF_COUNT; a++) regions.push_back({ a * old_cells * sizeof(uint16_t), a * new_cells * sizeof(uint16_t), old_cells * sizeof(uint16_t) });
        vkCmdCopyBuffer(p_cmd, state16.buffer, half.buffer, (uint32_t)regions.size(), regions.data());
        _barrier(p_cmd, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        const VkBufferCopy sr{ 0, 0, (VkDeviceSize)capacity * sizeof(uint32_t) };
        vkCmdCopyBuffer(p_cmd, stats.buffer, st.buffer, 1, &sr);
    }
    _reset_sim();
    state = wide;
    state16 = half;
    stats = st;
    pool = _device_buffer((VkDeviceSize)cap * SAMPLE_BYTES, 0, "WaterPool");
    masks = _device_buffer((VkDeviceSize)cap * MASK_BYTES, 0, "WaterMasks");
    bounds = _device_buffer((VkDeviceSize)cap * 2 * sizeof(uint32_t), 0, "WaterBounds");
    capacity = cap;
}

void WaterFeature::_parse_readback(uint32_t p_frame, WaterGpuLink& r_link)
{
    Readback& rd = pending[p_frame];
    Buffer& rb = readback[p_frame];
    if (rb.mapped && rd.session == r_link.session && (!rd.tiles.empty() || rd.stats_count)) {
        ctx->dd->buffer_invalidate(rb);
        const uint8_t* base = (const uint8_t*)rb.mapped;
        for (const Pending& p : rd.tiles) {
            WaterGpuResult& r = r_link.results.emplace_back();
            r.layout = p.layout;
            r.tile = p.tile;
            std::memcpy(r.data, base + p.offset, sizeof(r.data));
        }
        if (rd.stats_count) {
            r_link.stats_layout = rd.layout;
            r_link.stats.resize(rd.stats_count);
            std::memcpy(r_link.stats.data(), base + rd.stats_offset, rd.stats_count * sizeof(float));
        }
    }
    rd.tiles.clear();
    rd.stats_count = 0;
}

struct WaterHostWriter
{
    uint8_t* base = nullptr;
    VkDeviceAddress address = 0;
    VkDeviceSize offset = 0;

    template <typename T>
    VkDeviceAddress put(const T* p_data, size_t p_count) {
        offset = (offset + 15) & ~(VkDeviceSize)15;
        const VkDeviceAddress at = address + offset;
        if (p_count) std::memcpy(base + offset, p_data, p_count * sizeof(T));
        offset += std::max<size_t>(p_count, 1) * sizeof(T);
        return at;
    }
};

void WaterFeature::_record(RenderGraph::CommandList& cl, WaterGpuLink& r_link)
{
    VkCommandBuffer cmd = cl.cmd;
    const uint32_t f = cl.graph->current_frame;
    _item_count = 0;

    _ensure_sim(cmd, r_link);
    if (r_link.frozen.layout != frozen_layout || r_link.frozen.capacity != frozen_capacity) {
        _retire(frozen_pool);
        _retire(frozen_masks);
        frozen_layout = r_link.frozen.layout;
        frozen_capacity = r_link.frozen.capacity;
        if (frozen_capacity) {
            frozen_pool = _device_buffer((VkDeviceSize)frozen_capacity * SAMPLE_BYTES, 0, "WaterFrozenPool");
            frozen_masks = _device_buffer((VkDeviceSize)frozen_capacity * MASK_BYTES, 0, "WaterFrozenMasks");
        }
    }

    const bool live = capacity > 0 && r_link.tiles_w > 0;
    const uint32_t steps = live && !r_link.awake.empty() ? std::min(r_link.steps, std::min(r_link.max_steps, WaterGpuLink::BAKE_STEPS)) : 0u;
    const bool gathering = live && !r_link.awake.empty() && (steps || r_link.collect);

    std::vector<uint32_t> gather;
    Readback& rd = pending[f];
    rd.tiles.clear();
    rd.stats_count = 0;
    rd.stats_offset = 0;
    rd.session = r_link.session;
    rd.layout = r_link.layout;
    if (gathering) {
        const size_t count = r_link.awake.size();
        const size_t take = std::min<size_t>(count, READBACK_MAX);
        const size_t start = count > READBACK_MAX ? readback_cursor % count : 0;
        for (size_t k = 0; k < take; k++) {
            const ivec2 lt = unpack_tile(r_link.awake[(start + k) % count]);
            const int32_t slot = water::wrap(r_link.origin_tile.y + lt.y, r_link.slots) * r_link.slots + water::wrap(r_link.origin_tile.x + lt.x, r_link.slots);
            const uint32_t phys = r_link.page[slot] & PAGE_PHYS_MASK;
            if (phys == PAGE_EMPTY || phys >= capacity) continue;
            rd.tiles.push_back({ r_link.session, r_link.layout, r_link.origin_tile + lt, gather.size() * READBACK_ENTRY });
            gather.push_back(phys);
        }
        readback_cursor += (uint32_t)take;
        rd.stats_offset = gather.size() * READBACK_ENTRY;
        rd.stats_count = capacity;
    }
    Buffer& rb = readback[f];
    const VkDeviceSize rb_bytes = std::max<VkDeviceSize>(rd.stats_offset + (VkDeviceSize)rd.stats_count * sizeof(uint32_t), 16);
    if (ctx->dd->buffer_ensure_capacity(rb, rb_bytes) != Error::OK) {
        rd.tiles.clear();
        rd.stats_count = 0;
        gather.clear();
    }

    const VkDeviceSize host_bytes = 4096 + sizeof(WaterLevelInfo) + r_link.items.size() * sizeof(WaterItemGpu)
        + (r_link.page.size() + r_link.awake.size() + r_link.init.size() + r_link.surface.size() + gather.size()) * sizeof(uint32_t)
        + (r_link.sources.size() + r_link.events.size()) * sizeof(WaterGpuCell);
    Buffer& hb = host[f];
    if (ctx->dd->buffer_ensure_capacity(hb, host_bytes) != Error::OK || !hb.mapped) {
        rd.tiles.clear();
        rd.stats_count = 0;
        return;
    }
    WaterHostWriter hw{ (uint8_t*)hb.mapped, hb.device_address, 0 };
    WaterLevelInfo info{};
    info.state = state.device_address;
    info.state16 = state16.device_address;
    info.stats = stats.device_address;
    info.page = hw.put(r_link.page.data(), r_link.page.size());
    info.awake = hw.put(r_link.awake.data(), r_link.awake.size());
    info.init = hw.put(r_link.init.data(), r_link.init.size());
    info.surface = hw.put(r_link.surface.data(), r_link.surface.size());
    info.sources = hw.put(r_link.sources.data(), r_link.sources.size());
    info.events = hw.put(r_link.events.data(), r_link.events.size());
    info.pool = pool.device_address;
    info.masks = masks.device_address;
    info.bounds = bounds.device_address;
    info.gather = hw.put(gather.data(), gather.size());
    info.readback = rb.device_address;
    info.origin_x = r_link.origin_tile.x;
    info.origin_y = r_link.origin_tile.y;
    info.slots = r_link.slots;
    info.tiles_w = r_link.tiles_w;
    info.n = r_link.tiles_w * TILE;
    info.cells = capacity * TILE_CELLS;
    info.awake_count = (uint32_t)r_link.awake.size();
    info.init_count = (uint32_t)r_link.init.size();
    info.surface_count = (uint32_t)r_link.surface.size();
    info.source_count = (uint32_t)r_link.sources.size();
    info.event_count = (uint32_t)r_link.events.size();
    info.gather_count = (uint32_t)gather.size();
    const WaterGpuParams& prm = r_link.params;
    info.dx = prm.dx;
    info.gravity = prm.gravity;
    info.manning = prm.manning;
    info.vorticity = prm.vorticity;
    info.vortex_min_depth = prm.vortex_min_depth;
    info.cfl_limit = prm.cfl_limit;
    info.flux_depth_cap = prm.flux_depth_cap;
    info.foam_decay = prm.foam_decay;
    info.foam_gain = prm.foam_gain;
    const VkDeviceAddress info_addr = hw.put(&info, 1);

    std::vector<WaterItemGpu> items;
    items.reserve(r_link.items.size());
    for (const WaterGpuItem& it : r_link.items) {
        WaterItemGpu g{};
        if (it.kind == WaterGpuItem::FROZEN) {
            if (!frozen_pool.buffer || it.slot >= frozen_capacity) continue;
            g.samples = frozen_pool.device_address + (VkDeviceAddress)it.slot * SAMPLE_BYTES;
            g.masks = frozen_masks.device_address + (VkDeviceAddress)it.slot * MASK_BYTES;
        } else {
            if (!live || it.slot >= capacity) continue;
            g.samples = pool.device_address + (VkDeviceAddress)it.slot * SAMPLE_BYTES;
            g.masks = masks.device_address + (VkDeviceAddress)it.slot * MASK_BYTES;
            g.bounds = bounds.device_address + (VkDeviceAddress)it.slot * 2 * sizeof(uint32_t);
            g.has_bounds = 1;
        }
        g.origin_x = it.origin.x;
        g.origin_y = it.origin.y;
        g.spacing = it.spacing;
        g.ymin = it.ymin;
        g.ymax = it.ymax;
        items.push_back(g);
    }
    _items_addr = hw.put(items.data(), items.size());
    ctx->dd->buffer_flush(hb, 0, hw.offset);

    if (steps) vkCmdFillBuffer(cmd, stats.buffer, 0, VK_WHOLE_SIZE, 0u);
    _all_barrier(cmd);

    const uint32_t flags = prm.vorticity > 0.0f ? FLAG_VORTEX : 0u;
    const auto run = [&](uint32_t p_pass, uint32_t p_groups, float p_dt) {
        if (!p_groups) return;
        WaterSimPush pc{ info_addr, p_pass, p_dt, flags, 0u };
        ctx->dd->command_bind_push_constants(cmd, sizeof(pc), &pc);
        cl.dispatch("Water", p_groups, 1, 1);
        _compute_barrier(cmd);
    };
    const auto groups256 = [](size_t p_count) { return (uint32_t)((p_count + 255) / 256); };

    if (live && !r_link.init.empty()) {
        ctx->dd->command_bind_pipeline(cmd, sim_pipe);
        run(P_INIT, (uint32_t)r_link.init.size(), 0.0f);
    }

    Buffer& sb = staging[f];
    const VkDeviceSize staging_bytes = 16 + r_link.blob.size() * sizeof(float) + r_link.frozen.samples.size() * sizeof(vec4) + r_link.frozen.masks.size() * sizeof(uint32_t);
    if (ctx->dd->buffer_ensure_capacity(sb, staging_bytes) == Error::OK && sb.mapped) {
        uint8_t* dst = (uint8_t*)sb.mapped;
        VkDeviceSize at = 0;
        if (live && !r_link.uploads.empty()) {
            const VkDeviceSize cells = (VkDeviceSize)capacity * TILE_CELLS;
            std::vector<VkBufferCopy> wide, half;
            for (const WaterGpuUpload& u : r_link.uploads) {
                if (u.phys >= capacity) continue;
                const float* src = r_link.blob.data() + u.offset;
                if (u.array < WIDE_COUNT) {
                    std::memcpy(dst + at, src, TILE_BYTES);
                    wide.push_back({ at, ((VkDeviceSize)u.array * cells + (VkDeviceSize)u.phys * TILE_CELLS) * sizeof(float), TILE_BYTES });
                } else {
                    uint16_t* h16 = (uint16_t*)(dst + at);
                    for (int32_t k = 0; k < TILE_CELLS; k++) h16[k] = glm::packHalf1x16(src[k]);
                    half.push_back({ at, ((VkDeviceSize)(u.array - WIDE_COUNT) * cells + (VkDeviceSize)u.phys * TILE_CELLS) * sizeof(uint16_t), TILE_CELLS * sizeof(uint16_t) });
                }
                at += TILE_BYTES;
            }
            if (!wide.empty()) vkCmdCopyBuffer(cmd, sb.buffer, state.buffer, (uint32_t)wide.size(), wide.data());
            if (!half.empty()) vkCmdCopyBuffer(cmd, sb.buffer, state16.buffer, (uint32_t)half.size(), half.data());
        }
        const WaterGpuFrozen& fz = r_link.frozen;
        if (frozen_pool.buffer && !fz.slots.empty()) {
            const VkDeviceSize sample_base = at;
            std::memcpy(dst + at, fz.samples.data(), fz.samples.size() * sizeof(vec4));
            at += fz.samples.size() * sizeof(vec4);
            const VkDeviceSize mask_base = at;
            std::memcpy(dst + at, fz.masks.data(), fz.masks.size() * sizeof(uint32_t));
            at += fz.masks.size() * sizeof(uint32_t);
            std::vector<VkBufferCopy> samples, mregions;
            for (size_t k = 0; k < fz.slots.size(); k++) {
                if (fz.slots[k] >= frozen_capacity) continue;
                samples.push_back({ sample_base + k * SAMPLE_BYTES, (VkDeviceSize)fz.slots[k] * SAMPLE_BYTES, SAMPLE_BYTES });
                mregions.push_back({ mask_base + k * MASK_BYTES, (VkDeviceSize)fz.slots[k] * MASK_BYTES, MASK_BYTES });
            }
            if (!samples.empty()) {
                vkCmdCopyBuffer(cmd, sb.buffer, frozen_pool.buffer, (uint32_t)samples.size(), samples.data());
                vkCmdCopyBuffer(cmd, sb.buffer, frozen_masks.buffer, (uint32_t)mregions.size(), mregions.data());
            }
        }
        ctx->dd->buffer_flush(sb, 0, std::max<VkDeviceSize>(at, 16));
    }
    _all_barrier(cmd);

    if (steps) {
        ctx->dd->command_bind_pipeline(cmd, sim_pipe);
        const uint32_t awake = (uint32_t)r_link.awake.size();
        const uint32_t ring = groups256((size_t)4 * r_link.tiles_w * TILE);
        const uint32_t sources = groups256(r_link.sources.size());
        const uint32_t sub = std::clamp(prm.substeps, 1u, 8u);
        const float dt = STEP_DT / (float)sub;
        run(P_EVENTS, groups256(r_link.events.size()), 0.0f);
        for (uint32_t s = 0; s < steps; s++) {
            for (uint32_t k = 0; k < sub; k++) {
                run(P_ADVECT, awake, dt);
                run(P_CURLFORCE, awake, dt);
                run(P_GHOST, ring, dt);
                run(P_FLUXLIMIT, awake, dt);
                run(P_HEIGHT, awake, dt);
                run(P_DRAIN, awake, dt);
                run(P_VELOCITY, awake, dt);
                run(P_SOURCES, sources, dt);
                run(P_FOAM, awake, dt);
            }
        }
    }

    if (live && !r_link.surface.empty()) {
        ctx->dd->command_bind_pipeline(cmd, surface_pipe);
        VkDeviceAddress surface_push = info_addr;
        ctx->dd->command_bind_push_constants(cmd, sizeof(surface_push), &surface_push);
        cl.dispatch("Water Surface", (uint32_t)r_link.surface.size(), 1, 1);
    }
    _all_barrier(cmd);

    if (gathering && rd.stats_count) {
        if (!gather.empty()) {
            ctx->dd->command_bind_pipeline(cmd, sim_pipe);
            run(P_GATHER, (uint32_t)gather.size(), 0.0f);
        }
        const VkBufferCopy sr{ 0, rd.stats_offset, (VkDeviceSize)rd.stats_count * sizeof(uint32_t) };
        vkCmdCopyBuffer(cmd, stats.buffer, rb.buffer, 1, &sr);
        _barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_READ_BIT);
    }

    _item_count = (uint32_t)items.size();
    if (!_item_count) return;
    uint32_t cap = std::max(list_capacity, 64u);
    while (cap < _item_count * BLOCKS) cap *= 2;
    if (cap != list_capacity || !list.buffer) {
        _retire(list);
        list = _device_buffer((VkDeviceSize)cap * LOD_LEVELS * sizeof(uint32_t), 0, "WaterList");
        list_capacity = cap;
    }
    _draw_capacity = list_capacity;
    WaterDrawArgs init_args[LOD_LEVELS];
    for (uint32_t k = 0; k < LOD_LEVELS; k++) init_args[k] = { (1u << k) * (1u << k) * 6u, 0u, 0u, k * _draw_capacity };
    static_assert(LOD_LEVELS == 5);
    vkCmdUpdateBuffer(cmd, args.buffer, 0, sizeof(init_args), init_args);
    _all_barrier(cmd);

    auto* camera = cl.graph->buffer("Camera");
    auto* hiz = cl.graph->image_resource("HiZ") ? cl.graph->image("HiZ") : nullptr;
    auto* depth = cl.graph->image("G_Depth");
    WaterCullPush cp{};
    cp.camera = camera->device_address;
    cp.items = _items_addr;
    cp.args = args.device_address;
    cp.list = list.device_address;
    cp.count = _item_count;
    cp.capacity = _draw_capacity;
    cp.lod_error = r_link.lod_error;
    cp.min_quad = r_link.min_quad;
    cp.screen[0] = depth ? (float)depth->extent.width : 1.0f;
    cp.screen[1] = depth ? (float)depth->extent.height : 1.0f;
    cp.hiz_index = hiz ? hiz->bindless_sampled : 0;
    cp.hiz_mips = hiz ? hiz->mip_levels : 0;
    cp.hiz_enabled = hiz && depth ? 1u : 0u;
    _lod_error = r_link.lod_error;
    _caustics = r_link.caustics;
    _time = r_link.time;
    ctx->dd->command_bind_pipeline(cmd, cull_pipe);
    ctx->dd->command_bind_push_constants(cmd, sizeof(cp), &cp);
    cl.dispatch("Water Cull", (_item_count + 63) / 64, 1, 1);
    _barrier(cmd, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

void WaterFeature::_create_passes()
{
    sim_pass.name = "WaterSim";
    sim_pass.category = PASS_CATEGORY_EFFECTS;
    sim_pass.never_cull = true;
    sim_pass.setup = [](RenderGraph::Builder& b) {
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        if (b.graph->image_resource("HiZ")) b.read_image("HiZ", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    };
    sim_pass.execute = [this](RenderGraph::CommandList& cl) {
        WaterGpuLink* link = ctx->frame ? ctx->frame->water : nullptr;
        _item_count = 0;
        if (!link) return;
        frame_counter++;
        _free_retired(false);
        const uint32_t f = cl.graph->current_frame;
        if (link->session != session) {
            session = link->session;
            _reset_sim();
            layout = 0;
            _retire(frozen_pool);
            _retire(frozen_masks);
            frozen_layout = UINT64_MAX;
            frozen_capacity = 0;
            for (Readback& p : pending) {
                p.tiles.clear();
                p.stats_count = 0;
            }
        }
        _parse_readback(f, *link);
        _record(cl, *link);
        link->clear_commands();
        link->steps = 0;
    };

    backdrop_pass.name = "WaterBackdrop";
    backdrop_pass.category = PASS_CATEGORY_EFFECTS;
    backdrop_pass.setup = [](RenderGraph::Builder& b) {
        drivers::DeviceDriverVulkan::ImageCreateInfo ci{};
        ci.name = "WaterBackdrop";
        ci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        b.create_image("WaterBackdrop", ci);
        b.read_buffer("Camera", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
        b.read_image("SceneColor", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.read_image("G_Depth", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        b.write_image("WaterBackdrop", VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    };
    backdrop_pass.execute = [this](RenderGraph::CommandList& cl) {
        if (!_item_count) return;
        auto* color = cl.graph->image("SceneColor");
        auto* depth = cl.graph->image("G_Depth");
        auto* out = cl.graph->image("WaterBackdrop");
        auto* camera = cl.graph->buffer("Camera");
        if (!color || !depth || !out || !camera) return;
        WaterBackdropPush pc{};
        pc.camera = camera->device_address;
        pc.color_index = color->bindless_sampled;
        pc.depth_index = depth->bindless_sampled;
        pc.out_slot = out->bindless_storage;
        pc.width = out->extent.width;
        pc.height = out->extent.height;
        cl.dd->command_bind_pipeline(cl.cmd, backdrop_pipe);
        cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
        cl.dispatch("Water Backdrop", (pc.width + 7) / 8, (pc.height + 7) / 8, 1);
    };

    draw_pass.name = "WaterSurface";
    draw_pass.category = PASS_CATEGORY_RASTER;
    draw_pass.setup = [this](RenderGraph::Builder& b) {
        b.color_attachment("SceneColor", VK_ATTACHMENT_LOAD_OP_LOAD);
        _draw_setup(b);
    };
    draw_pass.execute = [this](RenderGraph::CommandList& cl) { _draw_execute(cl, 0); };

    debug_pass.name = "WaterDebug";
    debug_pass.category = PASS_CATEGORY_EDITOR;
    debug_pass.setup = [this](RenderGraph::Builder& b) {
        b.color_attachment("Viewport", VK_ATTACHMENT_LOAD_OP_LOAD);
        _draw_setup(b);
    };
    debug_pass.execute = [this](RenderGraph::CommandList& cl) { _draw_execute(cl, _debug_mode); };
}

void WaterFeature::_draw_setup(RenderGraph::Builder& b)
{
    b.depth_attachment("G_Depth", VK_ATTACHMENT_LOAD_OP_LOAD);
    b.read_buffer("Camera", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_UNIFORM_READ_BIT);
    b.read_buffer("DirectLight", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    if (b.graph->image_resource("WaterBackdrop")) b.read_image("WaterBackdrop", VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

void WaterFeature::_draw_execute(RenderGraph::CommandList& cl, uint32_t p_debug_mode)
{
    if (!_item_count || !list.buffer) return;
    auto* camera = cl.graph->buffer("Camera");
    auto* light = cl.graph->buffer("DirectLight");
    auto* backdrop = cl.graph->image("WaterBackdrop");
    auto* depth = cl.graph->image("G_Depth");
    if (!camera || !light || !backdrop || !depth) return;
    WaterDrawPush pc{};
    pc.camera = camera->device_address;
    pc.items = _items_addr;
    pc.list = list.device_address;
    pc.light = light->device_address;
    pc.capacity = _draw_capacity;
    pc.lod_error = _lod_error;
    pc.time = _time;
    pc.backdrop_index = backdrop->bindless_sampled;
    pc.width = backdrop->extent.width;
    pc.height = backdrop->extent.height;
    pc.flags = _caustics ? 1u : 0u;
    pc.debug_mode = p_debug_mode;
    cl.dd->command_render_set_viewport(cl.cmd, {{ {0, 0}, depth->extent }});
    cl.dd->command_render_set_scissor(cl.cmd, {{ {0, 0}, depth->extent }});
    cl.dd->command_bind_pipeline(cl.cmd, p_debug_mode ? debug_pipe : draw_pipe);
    cl.dd->command_bind_push_constants(cl.cmd, sizeof(pc), &pc);
    cl.dd->command_render_draw_indirect(cl.cmd, args, 0, LOD_LEVELS, sizeof(WaterDrawArgs));
}

void WaterFeature::add_debug(RenderGraph& g, uint32_t p_mode)
{
    if (!_built || !p_mode || !g.image_resource("Viewport")) return;
    _debug_mode = p_mode;
    g.add(&debug_pass);
}

Error WaterFeature::create_resources()
{
    const uint32_t frames = ctx->graph->frame_count;
    const auto make_host = [&](std::vector<Buffer>& r_list, VkDeviceSize p_size, bool p_read, const char* p_name) {
        r_list.resize(frames);
        for (Buffer& b : r_list) {
            drivers::DeviceDriverVulkan::BufferCreateInfo ci{};
            ci.size = p_size;
            ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            ci.device_local = false;
            ci.host_visible = true;
            ci.cpu_read = p_read;
            ci.name = p_name;
            b = ctx->dd->buffer_create(ci);
            if (!b.buffer) return false;
        }
        return true;
    };
    LUMEN_ERR_FAIL_COND_V(!make_host(host, 64 * 1024, false, "WaterHost"), Error::FAILED);
    LUMEN_ERR_FAIL_COND_V(!make_host(staging, 64 * 1024, false, "WaterStaging"), Error::FAILED);
    LUMEN_ERR_FAIL_COND_V(!make_host(readback, 64 * 1024, true, "WaterReadback"), Error::FAILED);
    pending.assign(frames, {});
    args = _device_buffer(LOD_LEVELS * sizeof(WaterDrawArgs), VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, "WaterArgs");
    LUMEN_ERR_FAIL_COND_V(!args.buffer, Error::FAILED);
    _create_passes();
    return Error::OK;
}

Error WaterFeature::create_pipelines()
{
    sim_pipe = _compute_pipeline(L"SHADERS_WATER_SIM_COMP", "water/sim.comp");
    surface_pipe = _compute_pipeline(L"SHADERS_WATER_SURFACE_COMP", "water/surface.comp");
    cull_pipe = _compute_pipeline(L"SHADERS_WATER_CULL_COMP", "water/cull.comp");
    backdrop_pipe = _compute_pipeline(L"SHADERS_WATER_BACKDROP_COMP", "water/backdrop.comp");

    VkRenderPass rp = ctx->graph->acquire_render_pass(draw_pass);
    EmbeddedResource::Blob vs_blob = EmbeddedResource::load(L"SHADERS_WATER_SURFACE_VERT");
    EmbeddedResource::Blob fs_blob = EmbeddedResource::load(L"SHADERS_WATER_SURFACE_FRAG");
    VkShaderModule vs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::VERTEX, .glsl = (const char*)vs_blob.data, .glsl_size = vs_blob.size, .name = "water/surface.vert" });
    VkShaderModule fs = ctx->dd->shader_create({ .stage = drivers::DeviceDriverVulkan::ShaderStage::FRAGMENT, .glsl = (const char*)fs_blob.data, .glsl_size = fs_blob.size, .name = "water/surface.frag" });
    drivers::DeviceDriverVulkan::GraphicsPipelineCreateInfo ci{};
    ci.vertex_shader = vs;
    ci.fragment_shader = fs;
    ci.render_pass = rp;
    ci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.front_face = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    ci.cull_mode = VK_CULL_MODE_FRONT_BIT;
    ci.depth_test = true;
    ci.depth_write = true;
    ci.depth_compare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    ci.blend_modes = { drivers::DeviceDriverVulkan::BlendMode::NONE };
    ci.name = "water/surface";
    draw_pipe = ctx->dd->graphics_pipeline_create(ci);
    ctx->graph->declare_image_format("Viewport", VK_FORMAT_R16G16B16A16_SFLOAT);
    ci.render_pass = ctx->graph->acquire_render_pass(debug_pass);
    ci.depth_write = false;
    ci.name = "water/surface_debug";
    debug_pipe = ctx->dd->graphics_pipeline_create(ci);
    ctx->dd->shader_free(vs);
    ctx->dd->shader_free(fs);
    return Error::OK;
}

void WaterFeature::destroy_resources()
{
    ctx->dd->pipeline_free(sim_pipe);
    ctx->dd->pipeline_free(surface_pipe);
    ctx->dd->pipeline_free(cull_pipe);
    ctx->dd->pipeline_free(backdrop_pipe);
    ctx->dd->pipeline_free(draw_pipe);
    ctx->dd->pipeline_free(debug_pipe);
    _reset_sim();
    layout = 0;
    _retire(frozen_pool);
    _retire(frozen_masks);
    _retire(args);
    _retire(list);
    for (Buffer& b : host) _retire(b);
    for (Buffer& b : staging) _retire(b);
    for (Buffer& b : readback) _retire(b);
    _free_retired(true);
    host.clear();
    staging.clear();
    readback.clear();
    pending.clear();
    list_capacity = 0;
    frozen_layout = UINT64_MAX;
    frozen_capacity = 0;
    session = UINT64_MAX;
}

void WaterFeature::build(RenderGraph& g)
{
    _built = false;
    if (host.empty() || !ctx->frame || !ctx->frame->water) return;
    if (!g.image_resource("G_Depth") || !g.image_resource("SceneColor")) return;
    g.add(&sim_pass);
    g.add(&backdrop_pass);
    g.add(&draw_pass);
    _built = true;
}

}
