#include <editor/docking/center_view/memory/memory_transients.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <core/base/utils.h>
#include <vulkan/vk_enum_string_helper.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <cstdio>

namespace lumen {

void MemoryTransients::draw(EditorContext& ctx)
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
            if (r.kind != RenderGraph::ResourceKind::Transient) continue;
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
                    if (r.kind != RenderGraph::ResourceKind::Transient || !r.image) continue;
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
            if (r.kind != RenderGraph::ResourceKind::Transient || !r.buffer) continue;
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
                    if (r.kind != RenderGraph::ResourceKind::Transient || !r.buffer) continue;
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

}