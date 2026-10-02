#include <core/rendering/render_graph_profiler.h>
#include <core/base/cpu_profiler.h>
#include <algorithm>

namespace lumen {

/***************/
/**** SETUP ****/
/***************/

Error RenderGraphProfiler::initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count)
{
    using enum Error;

    dd = &r_dd;
    period_ns = dd->physical_device_properties.limits.timestampPeriod;

    uint32_t valid_bits = dd->timestamp_valid_bits(dd->cd->graphics_queue_family);
    if (valid_bits == 0 || period_ns == 0.0) {
        supported = false;
        active = prev_active = false;
        log_write("RenderGraphProfiler: GPU timing unsupported (validBits=%u, period=%f).", valid_bits, period_ns);
        return Ok;
    }

    supported = true;
    stats_supported = supported && dd->physical_device_features.pipelineStatisticsQuery && dd->physical_device_features.occlusionQueryPrecise;
    valid_mask = (valid_bits >= 64) ? ~0ull : ((1ull << valid_bits) - 1);

    if (!stats_supported) {
        log_write("RenderGraphProfiler: extended stats UNSUPPORTED (pipelineStatisticsQuery=%d, occlusionQueryPrecise=%d).", (int)dd->physical_device_features.pipelineStatisticsQuery, (int)dd->physical_device_features.occlusionQueryPrecise);
    }

    slots.resize(p_frame_count);
    for (Slot& s : slots) {
        s.pool = dd->query_pool_create_timestamp(CAPACITY);
        if (stats_supported) {
            s.stat_pool = dd->query_pool_create_pipeline_statistics(CAPACITY, VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT | VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT);
            s.occl_pool = dd->query_pool_create_occlusion(CAPACITY);
        }
        s.marks.reserve(256);
    }

    raw_scratch.resize(CAPACITY);
    stat_scratch.resize(static_cast<size_t>(CAPACITY) * STAT_COUNT);
    occl_scratch.resize(CAPACITY);
    return Ok;
}

void RenderGraphProfiler::shutdown()
{
    for (Slot& s : slots) {
        dd->query_pool_free(s.pool);
        if (stats_supported) {
            dd->query_pool_free(s.stat_pool);
            dd->query_pool_free(s.occl_pool);
        }
    }
    slots.clear();
    raw_scratch.clear();
    stat_scratch.clear();
    occl_scratch.clear();
    name_table.clear();
    _named_ids.clear();
    _clear_results();
    supported = false;
    stats_supported = false;
    active = prev_active = false;
    stats_active = false;
}

/***************/
/**** NAMES ****/
/***************/

uint64_t RenderGraphProfiler::intern(std::string_view p_s)
{
    uint64_t h = 1469598103934665603ull;
    for (char c : p_s) { h ^= static_cast<uint8_t>(c); h *= 1099511628211ull; }
    return h;
}

uint64_t RenderGraphProfiler::intern_named(std::string_view p_s)
{
    uint64_t id = intern(p_s);
    if (_named_ids.insert(id, 0)) name_table.emplace(id, std::string(p_s));
    return id;
}

uint64_t RenderGraphProfiler::draw_key(uint64_t p_pass_name_id, uint64_t p_name_id, uint32_t p_occurrence)
{
    uint64_t h = p_pass_name_id;
    h ^= p_name_id + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    h ^= static_cast<uint64_t>(p_occurrence) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

const char* RenderGraphProfiler::name_of(uint64_t p_id) const
{
    auto it = name_table.find(p_id);
    return (it != name_table.end()) ? it->second.c_str() : "?";
}

/*******************/
/**** RECORDING ****/
/*******************/

void RenderGraphProfiler::_clear_results()
{
    results.clear();
    smoothed.clear();
    total_ms = 0.0;
    total_draws = 0;
    truncated = false;
    last_query_count = 0;
    last_stat_count = 0;
}

bool RenderGraphProfiler::_write_boundary(VkCommandBuffer p_cmd, uint32_t& r_index)
{
    Slot& s = slots[slot];
    if (s.query_count >= s.reset_count) { s.overflowed = true; return false; }
    r_index = s.query_count++;
    dd->command_write_timestamp(p_cmd, s.pool, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, r_index);
    return true;
}

void RenderGraphProfiler::_resolve()
{
    using enum Error;
    if (!active) return;
    if (frozen) return;

    Slot& s = slots[slot];
    if (!s.recorded) return;
    if (s.query_count < 2 || s.marks.empty()) { results.clear(); total_ms = 0.0; total_draws = 0; return; }

    if (dd->query_pool_get_results(s.pool, 0, s.query_count, raw_scratch.data()) != Ok) return;

    last_query_count = s.query_count;
    last_stat_count = s.stat_count;
    truncated = s.overflowed;

    bool have_stats = s.stats_recorded && s.stat_count > 0;
    if (have_stats) {
        Error e1 = dd->query_pool_get_results(s.stat_pool, 0, s.stat_count, stat_scratch.data(), STAT_COUNT);
        Error e2 = dd->query_pool_get_results(s.occl_pool, 0, s.occl_count, occl_scratch.data(), 1);
        if (e1 != Ok || e2 != Ok) have_stats = false;
    }

    results.clear();
    results.reserve(s.marks.size());

    auto delta_ms = [&](uint32_t p_a, uint32_t p_b) -> double {
        if (p_a == INVALID || p_b == INVALID) return 0.0;
        uint64_t a = raw_scratch[p_a] & valid_mask;
        uint64_t b = raw_scratch[p_b] & valid_mask;
        return (b >= a ? double(b - a) : 0.0) * period_ns * 1e-6;
    };

    double sum = 0.0;
    uint32_t draws = 0;

    for (const Mark& m : s.marks) {
        const double gap = delta_ms(m.lead_query,  m.begin_query);
        const double dur = delta_ms(m.begin_query, m.end_query);

        auto [it, fresh] = smoothed.try_emplace(m.key, Smoothed{ gap, dur });
        Smoothed& acc = it->second;
        if (!fresh) {
            acc.gap += smoothing * (gap - acc.gap);
            acc.dur += smoothing * (dur - acc.dur);
        }

        if (m.kind == MarkKind::Pass || m.kind == MarkKind::Barrier) sum += acc.gap + acc.dur;
        if (m.kind == MarkKind::Draw) draws++;

        Timing t;
        t.key = m.key;
        t.name = (m.name_id != 0) ? name_of(m.name_id) : "";
        t.type = (m.type_id != 0) ? name_of(m.type_id) : "";
        t.category = (m.kind == MarkKind::Pass || m.kind == MarkKind::Barrier) ? name_of(m.cat_id) : "";
        t.gap_ms = acc.gap;
        t.gpu_ms = acc.dur;
        t.raw_gap_ms = gap;
        t.raw_ms = dur;
        t.draw_count = m.draw_count;
        t.ordinal = m.ordinal;
        t.parent = m.parent;
        t.kind = m.kind;

        if (m.kind == MarkKind::Draw && have_stats && m.stat_query != INVALID) {
            const uint64_t* st = &stat_scratch[static_cast<size_t>(m.stat_query) * STAT_COUNT];
            t.vertices = st[0];
            t.primitives = st[1];
            t.samples = (m.occl_query != INVALID) ? occl_scratch[m.occl_query] : 0;
        }
        t.instances = m.instances;

        if (m.kind == MarkKind::Draw && m.parent < results.size()) {
            Timing& p = results[m.parent];
            p.vertices += t.vertices;
            p.primitives += t.primitives;
            p.samples += t.samples;
            p.instances += t.instances;
        }

        results.push_back(t);
    }

    total_ms = sum;
    total_draws = draws;
}

void RenderGraphProfiler::frame_begin(VkCommandBuffer p_cmd, uint32_t p_slot)
{
    slot = p_slot;

    CpuProfiler& cpu = cpu_profiler();
    cpu.zone_begin("GPU Profiler Resolve");
    _resolve();
    cpu.zone_end();

    prev_active = active;
    active = supported && settings && settings->gpu_on();
    stats_active = active && stats_supported && settings->gpu_pipeline_stats_on();

    if (prev_active && !active) {
        _clear_results();
        for (Slot& s : slots) s.recorded = false;
    }
    if (!active) return;

    Slot& s = slots[slot];
    s.marks.clear();
    s.query_count = 0;
    s.stat_count = 0;
    s.occl_count = 0;
    s.open_pass = INVALID;
    s.open_draw = INVALID;
    s.open_barrier = INVALID;
    s.pass_ordinal = 0;
    s.recorded = false;
    s.overflowed = false;

    uint32_t want = std::max(RESET_MIN, last_query_count * 2);
    s.reset_count = std::min(CAPACITY, want);
    dd->command_reset_query_pool(p_cmd, s.pool, 0, s.reset_count);

    if (stats_active) {
        uint32_t swant = std::max(RESET_MIN, last_stat_count * 2);
        s.stat_reset = std::min(CAPACITY, swant);
        dd->command_reset_query_pool(p_cmd, s.stat_pool, 0, s.stat_reset);
        dd->command_reset_query_pool(p_cmd, s.occl_pool, 0, s.stat_reset);
    }

    uint32_t t0 = 0;
    _write_boundary(p_cmd, t0);
    s.last_boundary = t0;
}

void RenderGraphProfiler::frame_end()
{
    if (!active) return;
    Slot& s = slots[slot];
    s.recorded = true;
    s.stats_recorded = stats_active;
}

void RenderGraphProfiler::pass_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_category)
{
    if (!active) return;
    Slot& s = slots[slot];

    Mark m;
    m.name_id = intern_named(p_name);
    m.cat_id = intern_named(p_category);
    m.key = m.name_id;
    m.lead_query = s.last_boundary;
    m.kind = MarkKind::Pass;

    uint32_t ts;
    if (_write_boundary(p_cmd, ts)) {
        m.begin_query = ts;
        s.last_boundary = ts;
    }

    s.open_pass = static_cast<uint32_t>(s.marks.size());
    s.open_draw = INVALID;
    s.pass_ordinal = 0;
    s.name_occurrence.clear();
    s.marks.push_back(m);
}

void RenderGraphProfiler::pass_end(VkCommandBuffer p_cmd, uint32_t p_draw_count)
{
    if (!active || slots[slot].open_pass == INVALID) return;
    Slot& s = slots[slot];
    Mark& m = s.marks[s.open_pass];

    m.draw_count = p_draw_count;

    uint32_t ts;
    if (_write_boundary(p_cmd, ts)) {
        m.end_query = ts;
        s.last_boundary = ts;
    }
    s.open_pass = INVALID;
    s.open_draw = INVALID;
}

void RenderGraphProfiler::sync_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_category)
{
    if (!active) return;
    Slot& s = slots[slot];

    Mark m;
    m.name_id = intern_named(p_name);
    m.cat_id = intern_named(p_category);
    m.key = m.name_id ^ 0xBA4713219876BEEFull;
    m.lead_query = s.last_boundary;
    m.parent = INVALID;
    m.kind = MarkKind::Barrier;

    uint32_t ts;
    if (_write_boundary(p_cmd, ts)) {
        m.begin_query = ts;
        s.last_boundary = ts;
    }

    s.open_barrier = static_cast<uint32_t>(s.marks.size());
    s.marks.push_back(m);
}

void RenderGraphProfiler::sync_end(VkCommandBuffer p_cmd)
{
    if (!active || slots[slot].open_barrier == INVALID) return;
    Slot& s = slots[slot];
    Mark& m = s.marks[s.open_barrier];

    uint32_t ts;
    if (_write_boundary(p_cmd, ts)) {
        m.end_query = ts;
        s.last_boundary = ts;
    }
    s.open_barrier = INVALID;
}

void RenderGraphProfiler::_leaf_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_type, MarkKind p_kind, uint32_t p_instances, bool p_query_stats)
{
    if (!active) return;
    Slot& s = slots[slot];
    if (s.open_pass == INVALID) return;

    const uint64_t pass_name_id = s.marks[s.open_pass].name_id;

    uint32_t occurrence;
    const uint64_t name_id = p_name.empty() ? 0 : intern_named(p_name);
    if (name_id != 0) {
        const uint32_t seen = s.name_occurrence.get(name_id);
        occurrence = (seen == IdMap::NONE) ? 0 : seen;
        s.name_occurrence.set(name_id, occurrence + 1);
    }
    else occurrence = s.pass_ordinal;

    const uint64_t type_id = p_name.empty() ? 0 : intern_named(p_type);

    Mark m;
    m.name_id = name_id;
    m.type_id = type_id;
    m.ordinal = s.pass_ordinal++;
    m.key = draw_key(pass_name_id, name_id, occurrence);
    m.lead_query = s.last_boundary;
    m.parent = s.open_pass;
    m.draw_count = 1;
    m.instances = p_instances;
    m.kind = p_kind;

    uint32_t ts;
    if (!_write_boundary(p_cmd, ts)) {
        s.open_draw = INVALID;
        return;
    }
    m.begin_query = ts;

    if (p_query_stats && stats_active && s.stat_count < s.stat_reset) {
        m.stat_query = s.stat_count++;
        m.occl_query = s.occl_count++;
        dd->command_begin_query(p_cmd, s.stat_pool, m.stat_query, 0);
        dd->command_begin_query(p_cmd, s.occl_pool, m.occl_query, VK_QUERY_CONTROL_PRECISE_BIT);
    }

    s.open_draw = static_cast<uint32_t>(s.marks.size());
    s.marks.push_back(m);
}

void RenderGraphProfiler::_leaf_end(VkCommandBuffer p_cmd)
{
    if (!active || slots[slot].open_draw == INVALID) return;
    Slot& s = slots[slot];
    Mark& m = s.marks[s.open_draw];

    if (stats_active && m.stat_query != INVALID) {
        dd->command_end_query(p_cmd, s.occl_pool, m.occl_query);
        dd->command_end_query(p_cmd, s.stat_pool, m.stat_query);
    }

    uint32_t ts;
    if (_write_boundary(p_cmd, ts)) {
        m.end_query = ts;
        s.last_boundary = ts;
    }
    s.open_draw = INVALID;
}

void RenderGraphProfiler::draw_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_type, uint32_t p_instances)
{
    _leaf_begin(p_cmd, p_name, p_type, MarkKind::Draw, p_instances, true);
}

void RenderGraphProfiler::draw_end(VkCommandBuffer p_cmd)
{
    _leaf_end(p_cmd);
}

void RenderGraphProfiler::dispatch_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_type)
{
    _leaf_begin(p_cmd, p_name, p_type, MarkKind::Dispatch, 0, false);
}

void RenderGraphProfiler::dispatch_end(VkCommandBuffer p_cmd)
{
    _leaf_end(p_cmd);
}

void RenderGraphProfiler::transfer_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_type)
{
    _leaf_begin(p_cmd, p_name, p_type, MarkKind::Transfer, 0, false);
}

void RenderGraphProfiler::transfer_end(VkCommandBuffer p_cmd)
{
    _leaf_end(p_cmd);
}

}