#include <editor/docking/center_view/debug_tabs/memory_tab.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <core/base/utils.h>
#include <vulkan/vk_enum_string_helper.h>
#include <imgui_internal.h>
#include <cstdio>

namespace lumen {

void MemoryDebugTab::_draw_transients(EditorContext& ctx)
{
    RenderGraph* graph = &ctx.renderer->graph;

    const ImVec4 warn_color = ImVec4(0.82f, 0.66f, 0.27f, 1.0f);

    // Transient images.
    {
        uint32_t free_count = 0;
        uint64_t free_bytes = 0;
        if (graph->current_frame < graph->image_transient_pools.size()) {
            for (auto& [key, imgs] : graph->image_transient_pools[graph->current_frame].free) {
                free_count += (uint32_t)imgs.size();
                for (const auto& img : imgs) free_bytes += img.mem_req.size;
            }
        }

        uint32_t live_count = 0;
        uint64_t live_bytes = 0;
        for (const RenderGraph::ImageResource& r : graph->image_resources) {
            if (r.kind != RenderGraph::ResourceKind::TRANSIENT) continue;
            if (!r.image) continue;
            ++live_count;
            live_bytes += r.image->mem_req.size;
        }

        imgui_title("Transient Image Pool");
        if (live_count == 0) {
            ImGui::TextDisabled("(none live)");
        } else {
            ImGui::TextDisabled("live %u | free %u | peak %u | reused %u/%u", live_count, free_count, graph->image_peak_live, graph->image_reuse_hits, graph->image_reuse_hits + graph->image_reuse_misses);
            if (ImGui::BeginTable("transient_img", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
                ImGui::TableSetupColumn("Transient Images", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("format", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("dims", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 50);
                ImGui::TableHeadersRow();
                uint32_t shown = 0;
                for (const RenderGraph::ImageResource& r : graph->image_resources) {
                    if (r.kind != RenderGraph::ResourceKind::TRANSIENT || !r.image) continue;
                    if (shown++ >= max_rows) continue;
                    auto it = graph->debug_names.find(r.name_id);
                    const char* name = (it != graph->debug_names.end()) ? it->second.c_str() : "<unnamed>";
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text(name);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(string_VkFormat(r.image->format));
                    ImGui::TableSetColumnIndex(2); ImGui::Text("%ux%u", r.image->extent.width, r.image->extent.height);
                    ImGui::TableSetColumnIndex(3); ImGui::Text(fmt_bytes(r.image->mem_req.size));
                }
                ImGui::EndTable();
            }
            if (live_count > max_rows) ImGui::TextDisabled("... (+%u more)", live_count - max_rows);
            ImGui::TextDisabled("live %s | retained in free-lists %s", fmt_bytes(live_bytes), fmt_bytes(free_bytes));
        }
    }
    
    imgui_section_gap();

    // Transient buffers.
    {
        uint32_t free_count = 0;
        uint64_t free_bytes = 0;
        if (graph->current_frame < graph->buffer_transient_pools.size()) {
            for (auto& [key, bufs] : graph->buffer_transient_pools[graph->current_frame].free) {
                free_count += (uint32_t)bufs.size();
                for (const auto& b : bufs) free_bytes += b.capacity;
            }
        }
    
        uint32_t live_count = 0;
        uint64_t live_logical = 0, live_capacity = 0;
        for (const RenderGraph::BufferResource& r : graph->buffer_resources) {
            if (r.kind != RenderGraph::ResourceKind::TRANSIENT || !r.buffer) continue;
            ++live_count;
            live_logical  += r.transient_storage.size;
            live_capacity += r.transient_storage.capacity;
        }

        imgui_title("Transient Buffer Pool");
        if (live_count == 0) {
            ImGui::TextDisabled("(none live)");
        } else {
            ImGui::TextDisabled("live %u | free %u | peak %u | reused %u/%u", live_count, free_count, graph->buffer_peak_live, graph->buffer_reuse_hits, graph->buffer_reuse_hits + graph->buffer_reuse_misses);
            if (ImGui::BeginTable("transient_buf", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
                ImGui::TableSetupColumn("Transient Buffers", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("memory", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("requested", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("allocated", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableHeadersRow();
                uint32_t shown = 0;
                for (const RenderGraph::BufferResource& r : graph->buffer_resources) {
                    if (r.kind != RenderGraph::ResourceKind::TRANSIENT || !r.buffer) continue;
                    if (shown++ >= max_rows) continue;
                    auto it = graph->debug_names.find(r.name_id);
                    const char* name = (it != graph->debug_names.end()) ? it->second.c_str() : "<unnamed>";
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::Text(name);
                    // ImGui::TableSetColumnIndex(1); ImGui::Text(r.transient_storage.memory == drivers::DeviceDriverVulkan::BufferCreateInfo::Memory::HostVisible ? "HostVisible" : "DeviceLocal");
                    ImGui::TableSetColumnIndex(1); ImGui::Text("NULL");
                    ImGui::TableSetColumnIndex(2); ImGui::Text(fmt_bytes(r.transient_storage.size));
                    ImGui::TableSetColumnIndex(3); if (r.transient_storage.capacity > r.transient_storage.size * 2) ImGui::TextColored(warn_color, "%s", fmt_bytes(r.transient_storage.capacity)); else ImGui::Text(fmt_bytes(r.transient_storage.capacity));
                }
                ImGui::EndTable();
            }
            if (live_count > max_rows) ImGui::TextDisabled("... (+%u more)", live_count - max_rows);
            ImGui::TextDisabled("requested %s | allocated %s | retained in free-lists %s", fmt_bytes(live_logical), fmt_bytes(live_capacity), fmt_bytes(free_bytes));
        }
    }
}

void MemoryDebugTab::draw(EditorContext& ctx)
{
    drivers::DeviceDriverVulkan* dd = ctx.renderer->dd;

    const bool budget_ext_enabled = dd->memory_budget_enabled();

    VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
    vmaGetHeapBudgets(dd->allocator, budgets);
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(dd->physical_device, &mem_props);
    const uint32_t heap_count = mem_props.memoryHeapCount;
    if (detailed_frag.size() != heap_count) {
        detailed_frag.assign(heap_count, 0.0f);
        detailed_valid = false;
    }

    uint64_t peak_now = 0;
    for (uint32_t h = 0; h < heap_count; ++h) peak_now += budgets[h].usage;
    if (peak_now > peak_bytes) peak_bytes = peak_now;
    
    const ImVec4 warn_color = ImVec4(0.82f, 0.66f, 0.27f, 1.0f);

    ImGui::Text("Frame %llu", (unsigned long long)frame_counter++);
    ImGui::SameLine();
    {
        float gap = ImGui::GetStyle().ItemInnerSpacing.x;
        float tri = ImGui::GetFontSize() * 0.5f;
        float wA = ImGui::CalcTextSize("peak").x;
        float wB = ImGui::CalcTextSize(fmt_bytes(peak_bytes)).x;
        float need = wA + gap + tri + gap + wB;
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > need) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - need);

        ImGui::TextDisabled("peak");
        ImGui::SameLine(0, gap);
        ImVec2 c = ImGui::GetCursorScreenPos();
        float h = ImGui::GetFontSize();
        ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(c.x, c.y + h * 0.72f), ImVec2(c.x + tri, c.y + h * 0.72f), ImVec2(c.x + tri * 0.5f,c.y + h * 0.28f), IM_COL32(210,120,90,255));
        ImGui::Dummy(ImVec2(tri, h));
        ImGui::SameLine(0, gap);
        ImGui::TextColored(ImVec4(0.90f, 0.55f, 0.40f, 1.0f), "%s", fmt_bytes(peak_bytes));
    }
    ImGui::Dummy(ImVec2(0, 4));

    auto heap_label = [](VkMemoryHeapFlags flags, uint32_t index, char* buf, size_t n) -> const char* {
        std::snprintf(buf, n, "%u  %s", index, (flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? "DEVICE_LOCAL" : "HOST");
        return buf;
    };

    auto bar = [&](const char* label, uint64_t used, uint64_t total) {
        float pct = total ? float(double(used) / double(total)) : 0.0f;
        ImGui::Text("%-30s %10s / %10s   %d%%", label, fmt_bytes(used), fmt_bytes(total), int(pct * 100.0f + 0.5f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float fullW  = ImGui::GetContentRegionAvail().x;
        float h = ImGui::GetFontSize();
        dl->AddRectFilled(p, ImVec2(p.x + fullW, p.y + h), IM_COL32(38,40,46,255), 3.0f);
        dl->AddRectFilled(p, ImVec2(p.x + fullW * ImMin(pct, 1.0f), p.y + h), imgui_pct_col(pct), 3.0f);
        ImGui::Dummy(ImVec2(fullW, h));
    };

    for (uint32_t h = 0; h < heap_count; h++) {
        char label[32];
        heap_label(mem_props.memoryHeaps[h].flags, h, label, sizeof(label));
        uint64_t total = budgets[h].budget ? budgets[h].budget : mem_props.memoryHeaps[h].size;
        bar(label, budgets[h].usage, total);
        if (h + 1 < heap_count) ImGui::Dummy(ImVec2(0, 4));
    }

    if (!budget_ext_enabled) ImGui::TextDisabled("VK_EXT_memory_budget unavailable - totals are VMA estimates.");

    imgui_section_gap();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float left_width = (avail.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    float right_width = avail.x - ImGui::GetStyle().ItemSpacing.x - left_width;

    ImGui::BeginChild("Left", ImVec2(left_width, 0), ImGuiChildFlags_Borders);
    {
        if (ImGui::BeginTable("heaps", 5, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableSetupColumn("Heaps", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("size", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("allocs", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("blocks", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("frag", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableHeadersRow();
            for (uint32_t h = 0; h < heap_count; ++h) {
                char label[32];
                heap_label(mem_props.memoryHeaps[h].flags, h, label, sizeof(label));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text(label);
                ImGui::TableSetColumnIndex(1); ImGui::Text(fmt_bytes(budgets[h].statistics.blockBytes));
                ImGui::TableSetColumnIndex(2); ImGui::Text("%u", budgets[h].statistics.allocationCount);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%u", budgets[h].statistics.blockCount);
                ImGui::TableSetColumnIndex(4); if (detailed_valid) ImGui::Text("%.1f%%", detailed_frag[h] * 100.0f); else ImGui::Text("-");
            }
            ImGui::EndTable();
        }

        if (ImGui::SmallButton("Refresh fragmentation")) {
            VmaTotalStatistics total{};
            vmaCalculateStatistics(dd->allocator, &total);
            for (uint32_t h = 0; h < heap_count; h++) {
                const VmaDetailedStatistics& d = total.memoryHeap[h];
                uint64_t free_bytes = d.statistics.blockBytes - d.statistics.allocationBytes;
                detailed_frag[h] = (d.statistics.blockBytes > 0) ? float(double(free_bytes) / double(d.statistics.blockBytes)) : 0.0f;
            }
            detailed_valid = true;
        }

        imgui_section_gap();
        struct PoolRow { const char* name; VmaPool pool; };
        const PoolRow pools[] = {
            { "Image Transient", dd->image_transient_pool },
            { "Image Persistent", dd->image_persistent_pool },
            { "Image Texture", dd->image_texture_pool },
            { "Buffer Geometry", dd->buffer_geometry_pool },
            { "Buffer Device", dd->buffer_device_pool },
            { "Buffer BAR", dd->buffer_bar_pool },
            { "Upload", dd->upload_pool },
            { "Readback", dd->readback_pool },
        };
        if (ImGui::BeginTable("pools", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoBordersInBody)) {
            ImGui::TableSetupColumn("VMA Pools", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("used", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("committed", ImGuiTableColumnFlags_WidthFixed, 90);
            ImGui::TableSetupColumn("allocs", ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableHeadersRow();
            for (const PoolRow& row : pools) {
                if (!row.pool) continue;
                VmaStatistics s{};
                vmaGetPoolStatistics(dd->allocator, row.pool, &s);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); imgui_tri_right(IM_COL32(150, 155, 165, 255)); ImGui::Text(row.name);
                ImGui::TableSetColumnIndex(1); ImGui::Text(fmt_bytes(s.allocationBytes));
                ImGui::TableSetColumnIndex(2); if (s.blockBytes > 0 && s.allocationBytes * 4 < s.blockBytes) ImGui::TextColored(warn_color, "%s", fmt_bytes(s.blockBytes)); else ImGui::Text(fmt_bytes(s.blockBytes));
                ImGui::TableSetColumnIndex(3); ImGui::Text("%u", s.allocationCount);
            }
            ImGui::EndTable();
        }

        imgui_section_gap();
        auto used = [](const drivers::DeviceDriverVulkan::IndexAllocator& a) { return a.high_water - (uint32_t)a.free_list.size(); };
        imgui_title("Bindless Heap");
        ImGui::TextDisabled("sampled %u/%u | storage %u/%u | sampler %u/%u", used(dd->bindless_heap.sampled_alloc), dd->bindless_heap.sampled_alloc.cap, used(dd->bindless_heap.storage_alloc), dd->bindless_heap.storage_alloc.cap, used(dd->bindless_heap.sampler_alloc), dd->bindless_heap.sampler_alloc.cap);
        imgui_title("Free-List Depth");
        ImGui::TextDisabled("sampled %zu | storage %zu | sampler %zu", dd->bindless_heap.sampled_alloc.free_list.size(), dd->bindless_heap.storage_alloc.free_list.size(), dd->bindless_heap.sampler_alloc.free_list.size());
    }
    ImGui::EndChild();

    ImGui::SameLine();
    
    ImGui::BeginChild("Right", ImVec2(right_width, 0), ImGuiChildFlags_Borders);
    {
        _draw_transients(ctx);
    }
    ImGui::EndChild();
}

}