#pragma once
#include <editor/layout/panel.h>
#include <drivers/vulkan/device_driver_vulkan.h>
#include <cstdint>
#include <vector>

namespace lumen {

struct RenderGraph;

struct MemoryPanel : Panel
{
    static constexpr uint32_t MAX_ROWS = 100;

    struct Heaps {
        VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
        VkPhysicalDeviceMemoryProperties props = {};
        uint32_t count = 0;
    };

    uint64_t frame_counter = 0;
    uint64_t peak_bytes = 0;
    std::vector<float> detailed_frag;
    bool detailed_valid = false;

    void _usage_bar(const char* p_label, uint64_t p_used, uint64_t p_total);

    void _draw_header();
    void _draw_budgets(const Heaps& p_heaps, bool p_budget_ext);
    void _draw_allocator(drivers::DeviceDriverVulkan& dd, const Heaps& p_heaps);
    void _draw_transient_images(const RenderGraph& graph);
    void _draw_transient_buffers(const RenderGraph& graph);

    const char* name() const override { return "Memory"; }
    DockZone default_zone() const override { return DockZone::BOTTOM; }
    bool default_open() const override { return false; }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar; }
    ImVec2 window_padding() const override { return ui_style().dock.drawer_padding; }
    void draw_contents(EditorContext& ctx) override;
};

}
