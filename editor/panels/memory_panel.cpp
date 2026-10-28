#include <editor/panels/memory_panel.h>
#include <core/base/strings.h>
#include <core/rendering/renderer.h>
#include <editor/ui/ui_style.h>
#include <editor/ui/ui_widgets.h>
#include <vulkan/vk_enum_string_helper.h>
#include <imgui_internal.h>
#include <cstdio>

namespace lumen {

static const char* _heap_label(VkMemoryHeapFlags p_flags, uint32_t p_index, char* r_buf, size_t p_size)
{
    std::snprintf(r_buf, p_size, "%u  %s", p_index, (p_flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "DEVICE_LOCAL" : "HOST");
    return r_buf;
}

void MemoryPanel::_usage_bar(const char* p_label, uint64_t p_used, uint64_t p_total)
{
    const UiStyle& ui = ui_style();
    const float pct = p_total ? float(double(p_used) / double(p_total)) : 0.0f;
    ImGui::Text("%-30s %10s / %10s   %d%%", p_label, fmt_bytes(p_used), fmt_bytes(p_total), int(pct * 100.0f + 0.5f));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float full_w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetFontSize();
    dl->AddRectFilled(p, ImVec2(p.x + full_w, p.y + h), ui.color.field, ui.data_table.bar_rounding);
    dl->AddRectFilled(p, ImVec2(p.x + full_w * ImMin(pct, 1.0f), p.y + h), ui_percent_color(pct), ui.data_table.bar_rounding);
    ImGui::Dummy(ImVec2(full_w, h));
}

void MemoryPanel::_draw_header()
{
    const UiStyle& ui = ui_style();
    ImGui::Text("Frame %llu", (unsigned long long)frame_counter++);
    ImGui::SameLine();

    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const float tri = ImGui::GetFontSize() * 0.5f;
    const float need = ImGui::CalcTextSize("peak").x + gap + tri + gap + ImGui::CalcTextSize(fmt_bytes(peak_bytes)).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > need) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - need);

    ImGui::TextDisabled("peak");
    ImGui::SameLine(0, gap);
    const ImVec2 c = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFontSize();
    ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(c.x, c.y + h * 0.72f), ImVec2(c.x + tri, c.y + h * 0.72f), ImVec2(c.x + tri * 0.5f, c.y + h * 0.28f), ui.color.notice);
    ImGui::Dummy(ImVec2(tri, h));
    ImGui::SameLine(0, gap);
    ImGui::TextColored(ui_vec4(ui.color.notice), "%s", fmt_bytes(peak_bytes));
    ImGui::Dummy(ImVec2(0, ui.data_table.block_gap));
}

void MemoryPanel::_draw_budgets(const Heaps& p_heaps, bool p_budget_ext)
{
    const UiStyle& ui = ui_style();
    for (uint32_t h = 0; h < p_heaps.count; h++) {
        char label[32];
        _heap_label(p_heaps.props.memoryHeaps[h].flags, h, label, sizeof(label));
        const uint64_t total = p_heaps.budgets[h].budget ? p_heaps.budgets[h].budget : p_heaps.props.memoryHeaps[h].size;
        _usage_bar(label, p_heaps.budgets[h].usage, total);
        if (h + 1 < p_heaps.count) ImGui::Dummy(ImVec2(0, ui.data_table.block_gap));
    }
    if (!p_budget_ext) ImGui::TextDisabled("VK_EXT_memory_budget unavailable - totals are VMA estimates.");
}

void MemoryPanel::_draw_allocator(drivers::DeviceDriverVulkan& dd, const Heaps& p_heaps)
{
    const UiStyle& ui = ui_style();
    const ImVec4 warn_color = ui_vec4(ui.color.warning);

    if (ImGui::BeginTable("heaps", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("Heaps", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_lg);
        ImGui::TableSetupColumn("allocs", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_sm);
        ImGui::TableSetupColumn("blocks", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_sm);
        ImGui::TableSetupColumn("frag", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_sm);
        ImGui::TableHeadersRow();
        for (uint32_t h = 0; h < p_heaps.count; ++h) {
            const VmaStatistics& stats = p_heaps.budgets[h].statistics;
            char label[32];
            _heap_label(p_heaps.props.memoryHeaps[h].flags, h, label, sizeof(label));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(fmt_bytes(stats.blockBytes));
            ImGui::TableSetColumnIndex(2); ImGui::Text("%u", stats.allocationCount);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%u", stats.blockCount);
            ImGui::TableSetColumnIndex(4);
            if (detailed_valid) ImGui::Text("%.1f%%", detailed_frag[h] * 100.0f);
            else ImGui::TextUnformatted("-");
        }
        ImGui::EndTable();
    }

    if (ImGui::SmallButton("Refresh fragmentation")) {
        VmaTotalStatistics total{};
        vmaCalculateStatistics(dd.allocator, &total);
        for (uint32_t h = 0; h < p_heaps.count; h++) {
            const VmaDetailedStatistics& d = total.memoryHeap[h];
            const uint64_t free_bytes = d.statistics.blockBytes - d.statistics.allocationBytes;
            detailed_frag[h] = d.statistics.blockBytes > 0 ? float(double(free_bytes) / double(d.statistics.blockBytes)) : 0.0f;
        }
        detailed_valid = true;
    }

    ui_section_gap();
    struct PoolRow { const char* name; VmaPool pool; };
    const PoolRow pools[] = {
        { "Image Transient", dd.image_transient_pool },
        { "Image Persistent", dd.image_persistent_pool },
        { "Image Texture", dd.image_texture_pool },
        { "Buffer Geometry", dd.buffer_geometry_pool },
        { "Buffer Device", dd.buffer_device_pool },
        { "Buffer BAR", dd.buffer_bar_pool },
        { "Upload", dd.upload_pool },
        { "Readback", dd.readback_pool },
    };
    if (ImGui::BeginTable("pools", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
        ImGui::TableSetupColumn("VMA Pools", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("used", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_lg);
        ImGui::TableSetupColumn("committed", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_lg);
        ImGui::TableSetupColumn("allocs", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_sm);
        ImGui::TableHeadersRow();
        for (const PoolRow& row : pools) {
            if (!row.pool) continue;
            VmaStatistics s{};
            vmaGetPoolStatistics(dd.allocator, row.pool, &s);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ui_tri_right(ui.color.text_dim); ImGui::TextUnformatted(row.name);
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(fmt_bytes(s.allocationBytes));
            ImGui::TableSetColumnIndex(2);
            if (s.blockBytes > 0 && s.allocationBytes * 4 < s.blockBytes) ImGui::TextColored(warn_color, "%s", fmt_bytes(s.blockBytes));
            else ImGui::TextUnformatted(fmt_bytes(s.blockBytes));
            ImGui::TableSetColumnIndex(3); ImGui::Text("%u", s.allocationCount);
        }
        ImGui::EndTable();
    }

    ui_section_gap();
    const drivers::DeviceDriverVulkan::BindlessHeap& heap = dd.bindless_heap;
    auto used = [](const drivers::DeviceDriverVulkan::IndexAllocator& a) { return a.high_water - (uint32_t)a.free_list.size(); };
    ui_title("Bindless Heap");
    ImGui::TextDisabled("sampled %u/%u | storage %u/%u | sampler %u/%u", used(heap.sampled_alloc), heap.sampled_alloc.cap, used(heap.storage_alloc), heap.storage_alloc.cap, used(heap.sampler_alloc), heap.sampler_alloc.cap);
    ui_title("Free-List Depth");
    ImGui::TextDisabled("sampled %zu | storage %zu | sampler %zu", heap.sampled_alloc.free_list.size(), heap.storage_alloc.free_list.size(), heap.sampler_alloc.free_list.size());
}

void MemoryPanel::_draw_transient_images(const RenderGraph& graph)
{
    const UiStyle& ui = ui_style();
    uint32_t free_count = 0;
    uint64_t free_bytes = 0;
    if (graph.current_frame < graph.image_transient_pools.size()) {
        for (auto& [key, imgs] : graph.image_transient_pools[graph.current_frame].free) {
            free_count += (uint32_t)imgs.size();
            for (const auto& img : imgs) free_bytes += img.mem_req.size;
        }
    }

    uint32_t live_count = 0;
    uint64_t live_bytes = 0;
    for (const RenderGraph::ImageResource& r : graph.image_resources) {
        if (r.kind != RenderGraph::ResourceKind::TRANSIENT || !r.image) continue;
        ++live_count;
        live_bytes += r.image->mem_req.size;
    }

    ui_title("Transient Image Pool");
    if (live_count == 0) {
        ImGui::TextDisabled("(none live)");
    } else {
        ImGui::TextDisabled("live %u | free %u | peak %u | reused %u/%u", live_count, free_count, graph.image_peak_live, graph.image_reuse_hits, graph.image_reuse_hits + graph.image_reuse_misses);
        if (ImGui::BeginTable("transient_img", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableSetupColumn("Transient Images", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("format", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("dims", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_md);
            ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_xs);
            ImGui::TableHeadersRow();
            uint32_t shown = 0;
            for (const RenderGraph::ImageResource& r : graph.image_resources) {
                if (r.kind != RenderGraph::ResourceKind::TRANSIENT || !r.image) continue;
                if (shown++ >= MAX_ROWS) continue;
                auto it = graph.debug_names.find(r.name_id);
                const char* name = (it != graph.debug_names.end()) ? it->second.c_str() : "<unnamed>";
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(string_VkFormat(r.image->format));
                ImGui::TableSetColumnIndex(2); ImGui::Text("%ux%u", r.image->extent.width, r.image->extent.height);
                ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(fmt_bytes(r.image->mem_req.size));
            }
            ImGui::EndTable();
        }
        if (live_count > MAX_ROWS) ImGui::TextDisabled("... (+%u more)", live_count - MAX_ROWS);
        ImGui::TextDisabled("live %s | retained in free-lists %s", fmt_bytes(live_bytes), fmt_bytes(free_bytes));
    }
}

void MemoryPanel::_draw_transient_buffers(const RenderGraph& graph)
{
    const UiStyle& ui = ui_style();
    const ImVec4 warn_color = ui_vec4(ui.color.warning);
    uint32_t free_count = 0;
    uint64_t free_bytes = 0;
    if (graph.current_frame < graph.buffer_transient_pools.size()) {
        for (auto& [key, bufs] : graph.buffer_transient_pools[graph.current_frame].free) {
            free_count += (uint32_t)bufs.size();
            for (const auto& b : bufs) free_bytes += b.capacity;
        }
    }

    uint32_t live_count = 0;
    uint64_t live_logical = 0;
    uint64_t live_capacity = 0;
    for (const RenderGraph::BufferResource& r : graph.buffer_resources) {
        if (r.kind != RenderGraph::ResourceKind::TRANSIENT || !r.buffer) continue;
        ++live_count;
        live_logical += r.transient_storage.size;
        live_capacity += r.transient_storage.capacity;
    }

    ui_title("Transient Buffer Pool");
    if (live_count == 0) {
        ImGui::TextDisabled("(none live)");
    } else {
        ImGui::TextDisabled("live %u | free %u | peak %u | reused %u/%u", live_count, free_count, graph.buffer_peak_live, graph.buffer_reuse_hits, graph.buffer_reuse_hits + graph.buffer_reuse_misses);
        if (ImGui::BeginTable("transient_buf", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableSetupColumn("Transient Buffers", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("memory", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("requested", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_md);
            ImGui::TableSetupColumn("allocated", ImGuiTableColumnFlags_WidthFixed, ui.data_table.column_md);
            ImGui::TableHeadersRow();
            uint32_t shown = 0;
            for (const RenderGraph::BufferResource& r : graph.buffer_resources) {
                if (r.kind != RenderGraph::ResourceKind::TRANSIENT || !r.buffer) continue;
                if (shown++ >= MAX_ROWS) continue;
                auto it = graph.debug_names.find(r.name_id);
                const char* name = (it != graph.debug_names.end()) ? it->second.c_str() : "<unnamed>";
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
                ImGui::TableSetColumnIndex(1); ImGui::Text("NULL");
                ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(fmt_bytes(r.transient_storage.size));
                ImGui::TableSetColumnIndex(3);
                if (r.transient_storage.capacity > r.transient_storage.size * 2) ImGui::TextColored(warn_color, "%s", fmt_bytes(r.transient_storage.capacity));
                else ImGui::TextUnformatted(fmt_bytes(r.transient_storage.capacity));
            }
            ImGui::EndTable();
        }
        if (live_count > MAX_ROWS) ImGui::TextDisabled("... (+%u more)", live_count - MAX_ROWS);
        ImGui::TextDisabled("requested %s | allocated %s | retained in free-lists %s", fmt_bytes(live_logical), fmt_bytes(live_capacity), fmt_bytes(free_bytes));
    }
}

void MemoryPanel::draw_contents(EditorContext& ctx)
{
    drivers::DeviceDriverVulkan& dd = *ctx.renderer->dd;

    Heaps heaps;
    vmaGetHeapBudgets(dd.allocator, heaps.budgets);
    vkGetPhysicalDeviceMemoryProperties(dd.physical_device, &heaps.props);
    heaps.count = heaps.props.memoryHeapCount;
    if (detailed_frag.size() != heaps.count) {
        detailed_frag.assign(heaps.count, 0.0f);
        detailed_valid = false;
    }
    uint64_t usage = 0;
    for (uint32_t h = 0; h < heaps.count; ++h) usage += heaps.budgets[h].usage;
    if (usage > peak_bytes) peak_bytes = usage;

    _draw_header();
    _draw_budgets(heaps, dd.memory_budget_enabled());
    ui_section_gap();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float left_width = (avail.x - spacing) * 0.5f;
    const float right_width = avail.x - spacing - left_width;

    ImGui::BeginChild("Left", ImVec2(left_width, 0), ImGuiChildFlags_Borders);
    _draw_allocator(dd, heaps);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("Right", ImVec2(right_width, 0), ImGuiChildFlags_Borders);
    _draw_transient_images(ctx.renderer->main_view.graph);
    ui_section_gap();
    _draw_transient_buffers(ctx.renderer->main_view.graph);
    ImGui::EndChild();
}

}
