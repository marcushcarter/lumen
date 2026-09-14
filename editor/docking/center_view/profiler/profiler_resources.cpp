#include <editor/docking/center_view/profiler/profiler_resources.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <vulkan/vk_enum_string_helper.h>
#include <imgui.h>
#include <algorithm>
#include <cstdio>

namespace lumen {

void ProfilerResources::draw(EditorContext& ctx, const char* p_pass_name)
{
    if (!p_pass_name || !p_pass_name[0]) {
        ImGui::TextDisabled("Select a pass in the timeline.");
        return;
    }

    RenderGraph& graph = ctx.renderer->graph;

    const RenderGraph::Node* node = nullptr;
    for (const RenderGraph::Node& n : graph.nodes) {
        if (n.pass && n.pass->name == p_pass_name) { node = &n; break; }
    }
    if (!node) {
        ImGui::TextDisabled("Pass '%s' not in current graph.", p_pass_name);
        return;
    }

    int writes = 0, reads = 0;
    for (const auto& a : node->image_accesses)  (a.is_write ? writes : reads)++;
    for (const auto& a : node->buffer_accesses) (a.is_write ? writes : reads)++;

    imgui_title("%s", p_pass_name);
    ImGui::SameLine();
    ImGui::TextDisabled("writes %d reads %d", writes, reads);
    imgui_spacing();

    auto name_of = [&](uint64_t id) -> const char* {
        auto it = graph.debug_names.find(id);
        return it != graph.debug_names.end() ? it->second.c_str() : "?";
    };
    auto kind_str = [](RenderGraph::ResourceKind k) {
        return k == RenderGraph::ResourceKind::Imported ? "Imported" : "Transient";
    };
    auto producer_name = [&](int prod) -> const char* {
        if (prod < 0 || prod >= (int)graph.nodes.size() || !graph.nodes[prod].pass) return "-";
        return graph.nodes[prod].pass->name.c_str();
    };

    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;

    // Images.
    ImGui::BeginChild("ResLeft", ImVec2(0, 0), ImGuiChildFlags_None);
    {
        imgui_title("Images");
        if (ImGui::BeginTable("img", 3, tf)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Kind",   ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Format");
            ImGui::TableHeadersRow();

            int row = 0;
            for (const RenderGraph::ImageAccess& a : node->image_accesses) {
                if (a.resource_index < 0) continue;
                const RenderGraph::ImageResource& r = graph.image_resources[a.resource_index];
                VkFormat format = (r.kind == RenderGraph::ResourceKind::Imported && r.image) ? r.image->format : r.image_create_info.format;

                ImGui::TableNextRow();
                ImGui::PushID(row++);
                ImGui::TableNextColumn(); ImGui::Selectable(name_of(r.name_id), false, ImGuiSelectableFlags_SpanAllColumns);
                const bool hov = ImGui::IsItemHovered();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(kind_str(r.kind));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(string_VkFormat(format));
                
                if (hov) {
                    ImGui::BeginTooltip();
                    imgui_title("%s", name_of(r.name_id));

                    const char* acc = a.is_write ? (a.is_attachment ? (a.is_depth ? "Write (depth attachment)" : "Write (color attachment)") : "Write") : "Read";
                    imgui_property_row("Access", "%s", acc);
                    imgui_property_row("Layout", "%s", string_VkImageLayout(a.layout));
                    if (a.is_attachment) {
                        imgui_property_row("Load",  "%s", string_VkAttachmentLoadOp(a.load_op));
                        imgui_property_row("Store", "%s", string_VkAttachmentStoreOp(a.store_op));
                    }
                    imgui_spacing();
                    imgui_property_row("Producer", "%s", producer_name(r.producer));
                    imgui_property_row("Lifetime", "pass %d-%d", r.first_use, r.last_use);
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    // Buffers.
    ImGui::BeginChild("ResRight", ImVec2(0, 0), ImGuiChildFlags_None);
    {
        imgui_title("Buffers");
        if (ImGui::BeginTable("buf", 2, tf)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            int row = 0;
            for (const RenderGraph::BufferAccess& a : node->buffer_accesses) {
                if (a.resource_index < 0) continue;
                const RenderGraph::BufferResource& r = graph.buffer_resources[a.resource_index];

                ImGui::TableNextRow();
                ImGui::PushID(row++);
                ImGui::TableNextColumn(); ImGui::Selectable(name_of(r.name_id), false, ImGuiSelectableFlags_SpanAllColumns);
                const bool hov = ImGui::IsItemHovered();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(kind_str(r.kind));
                
                if (hov) {
                    ImGui::BeginTooltip();
                    imgui_title("%s", name_of(r.name_id));
                    imgui_property_row("Access", "%s", a.is_write ? "Write" : "Read");
                    imgui_property_row("Producer", "%s", producer_name(r.producer));
                    imgui_property_row("Lifetime", "pass %d-%d", r.first_use, r.last_use);
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

}