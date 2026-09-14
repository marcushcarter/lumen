#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/rendering/render_graph_profiler.h>
#include <core/base/error.h>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace lumen {

struct RenderGraph
{
    /***************/
    /**** SETUP ****/
    /***************/
    
    drivers::DeviceDriverVulkan* dd = nullptr;
    
    uint32_t frame_count = 1;
    uint32_t current_frame = 0;
    uint32_t width = 0, height = 0;
    
    RenderGraphProfiler profiler;
    
    Error initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t frame_count);
    void shutdown();
    Error set_size(uint32_t p_width, uint32_t p_height);

    /***************/
    /**** NAMES ****/
    /***************/
    
    std::unordered_map<uint64_t, std::string> debug_names;
    
    static uint64_t intern(std::string_view p_name);
    uint64_t intern_named(std::string_view p_name);

    /*******************/
    /**** RESOURCES ****/
    /*******************/
    
    enum class ResourceKind { Imported, Transient };

    void release_transients();
    
    // ----- IMAGE -----

    struct ImageResource {
        ResourceKind kind = ResourceKind::Imported;
        uint64_t name_id = 0;

        drivers::DeviceDriverVulkan::Image* image = nullptr;
        drivers::DeviceDriverVulkan::Image transient_storage;
        drivers::DeviceDriverVulkan::ImageCreateInfo image_create_info;

        VkImageLayout final_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags2 final_stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        VkAccessFlags2 final_access = 0;

        int producer = -1;
        bool read = false;
        bool written = false;
        int first_use = -1;
        int last_use = -1;
    };

    struct ImageAccess {
        uint64_t name_id = 0;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkAccessFlags2 access = 0;
        bool is_write = false;
        int resource_index = -1;

        bool is_attachment = false;
        bool is_depth = false;
        VkAttachmentLoadOp load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        VkAttachmentStoreOp store_op = VK_ATTACHMENT_STORE_OP_STORE;
        VkClearValue clear{};
    };

    struct ImageBarrier {
        VkImage image = VK_NULL_HANDLE;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageLayout old_layout = VK_IMAGE_LAYOUT_UNDEFINED, new_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags2 src_stage = 0, dst_stage = 0;
        VkAccessFlags2 src_access = 0, dst_access = 0;
    };

    struct ImageTransientPool {
        std::unordered_map<uint64_t, std::vector<drivers::DeviceDriverVulkan::Image>> free;
    };

    std::vector<ImageResource> image_resources;
    std::unordered_map<uint64_t, uint32_t> image_resource_map;
    std::vector<ImageBarrier> final_image_barriers;
    std::vector<ImageTransientPool> image_transient_pools;
    std::unordered_map<uint64_t, VkFormat> declared_image_formats;

    uint32_t image_peak_live = 0;
    uint32_t image_reuse_hits = 0;
    uint32_t image_reuse_misses = 0;

    drivers::DeviceDriverVulkan::Image* image(std::string_view p_name);
    ImageResource* image_resource(std::string_view p_name);
    ImageResource* image_resource_by_id(uint64_t p_name_id);
    void import_image(std::string_view p_name, drivers::DeviceDriverVulkan::Image* p_image, VkImageLayout p_final_layout, VkPipelineStageFlags2 p_final_stage, VkAccessFlags2 p_final_access);
    
    void create_image(std::string_view p_name, const drivers::DeviceDriverVulkan::ImageCreateInfo& p_create_info);
    uint64_t _image_transient_key(const drivers::DeviceDriverVulkan::ImageCreateInfo& p_create_info, VkExtent2D p_extent);
    void _image_resolve_extent(const drivers::DeviceDriverVulkan::ImageCreateInfo& p_create_info, uint32_t& r_width, uint32_t& r_height);
    void _image_materialize_transient(ImageResource& r);
    void _image_release_transients();
    
    void declare_image_format(std::string_view p_name, VkFormat p_format);

    // ----- BUFFER -----

    struct BufferResource {
        ResourceKind kind = ResourceKind::Imported;
        uint64_t name_id = 0;

        drivers::DeviceDriverVulkan::Buffer* buffer = nullptr;
        drivers::DeviceDriverVulkan::Buffer transient_storage;
        drivers::DeviceDriverVulkan::BufferCreateInfo buffer_create_info;

        VkPipelineStageFlags2 final_stage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        VkAccessFlags2 final_access = 0;

        int producer = -1;
        bool read = false;
        bool written = false;
        int first_use = -1;
        int last_use = -1;
    };

    struct BufferAccess {
        uint64_t name_id = 0;
        VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        VkAccessFlags2 access = 0;
        bool is_write = false;
        int resource_index = -1;
    };

    struct BufferBarrier {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = VK_WHOLE_SIZE;
        VkPipelineStageFlags2 src_stage = 0, dst_stage = 0;
        VkAccessFlags2 src_access = 0, dst_access = 0;
    };

    struct BufferTransientPool {
        std::unordered_map<uint64_t, std::vector<drivers::DeviceDriverVulkan::Buffer>> free;
    };
    
    std::vector<BufferResource> buffer_resources;
    std::unordered_map<uint64_t, uint32_t> buffer_resource_map;
    std::vector<BufferBarrier> final_buffer_barriers;
    std::vector<BufferTransientPool> buffer_transient_pools;

    uint32_t buffer_peak_live = 0;
    uint32_t buffer_reuse_hits = 0;
    uint32_t buffer_reuse_misses = 0;
    
    drivers::DeviceDriverVulkan::Buffer* buffer(std::string_view p_name);
    BufferResource* buffer_resource(std::string_view p_name);
    BufferResource* buffer_resource_by_id(uint64_t p_name_id);
    void import_buffer(std::string_view p_name, drivers::DeviceDriverVulkan::Buffer* p_buffer, VkPipelineStageFlags2 p_final_stage, VkAccessFlags2 p_final_access);
    
    void create_buffer(std::string_view p_name, const drivers::DeviceDriverVulkan::BufferCreateInfo& p_create_info);
    uint64_t _buffer_transient_key(VkBufferUsageFlags p_usage, bool p_device_local, bool p_host_visible, bool p_cpu_read, VkDeviceSize p_capacity);
    void _buffer_materialize_transient(BufferResource& r);
    void _buffer_release_transients();
    
    /******************/
    /**** COMMANDS ****/
    /******************/
    
    struct CommandList {
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        RenderGraph* graph = nullptr;
        drivers::DeviceDriverVulkan* dd = nullptr;
        uint32_t node_index = 0;
        uint32_t draw_count = 0;

        void draw(std::string_view p_name, uint32_t p_vertex_count, uint32_t p_instance_count = 1, uint32_t p_base_vertex = 0, uint32_t p_first_instance = 0);

        // void draw_indexed(std::string_view p_name, uint32_t p_index_count, uint32_t p_instance_count = 1, uint32_t p_first_index = 0, int32_t p_vertex_offset = 0, uint32_t p_first_instance = 0) {
        //     graph->profiler.draw_begin(cmd, _name(p_name));
        //     dd->command_render_draw_indexed(cmd, p_index_count, p_instance_count, p_first_index, p_vertex_offset, p_first_instance);
        //     ++draw_count;
        //     graph->profiler.draw_end(cmd);
        // }

        // void draw_indexed_indirect(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_indirect, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride) {
        //     graph->profiler.draw_begin(cmd, _name(p_name));
        //     dd->command_render_draw_indexed_indirect(cmd, p_indirect, p_offset, p_draw_count, p_stride);
        //     ++draw_count;
        //     graph->profiler.draw_end(cmd);
        // }

        void draw_indexed_indirect_count(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_indirect, uint64_t p_offset, const drivers::DeviceDriverVulkan::Buffer& p_count, uint64_t p_count_offset, uint32_t p_max_draws, uint32_t p_stride);

        // void draw_indirect(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_indirect, uint64_t p_offset, uint32_t p_draw_count, uint32_t p_stride) {
        //     graph->profiler.draw_begin(cmd, _name(p_name));
        //     dd->command_render_draw_indirect(cmd, p_indirect, p_offset, p_draw_count, p_stride);
        //     ++draw_count;
        //     graph->profiler.draw_end(cmd);
        // }

        // void draw_indirect_count(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_indirect, uint64_t p_offset, const drivers::DeviceDriverVulkan::Buffer& p_count, uint64_t p_count_offset, uint32_t p_max_draws, uint32_t p_stride) {
        //     graph->profiler.draw_begin(cmd, _name(p_name));
        //     dd->command_render_draw_indirect_count(cmd, p_indirect, p_offset, p_count, p_count_offset, p_max_draws, p_stride);
        //     ++draw_count;
        //     graph->profiler.draw_end(cmd);
        // }

        void draw_indirect_count(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_indirect, uint64_t p_offset, const drivers::DeviceDriverVulkan::Buffer& p_count, uint64_t p_count_offset, uint32_t p_max_draws, uint32_t p_stride);

        void dispatch(std::string_view p_name, uint32_t p_x, uint32_t p_y = 1, uint32_t p_z = 1);
        void dispatch_indirect(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_indirect, uint64_t p_offset = 0);

        void fill_buffer(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_buffer, uint32_t p_value, VkDeviceSize p_offset = 0, VkDeviceSize p_size = VK_WHOLE_SIZE);
        void update_buffer(std::string_view p_name, const drivers::DeviceDriverVulkan::Buffer& p_buffer, const void* p_data, VkDeviceSize p_size, VkDeviceSize p_offset = 0);
    };
    
    /**************/
    /**** PASS ****/
    /**************/

    struct Pass;

    struct Builder {
        RenderGraph* graph = nullptr;
        Pass* pass = nullptr;
        uint32_t node_index = 0;

        void create_image(std::string_view p_name, const drivers::DeviceDriverVulkan::ImageCreateInfo& p_create_info);
        void read_image(std::string_view p_name, VkImageLayout p_layout, VkPipelineStageFlags2 p_stage, VkAccessFlags2 p_access);
        void read_all_images(VkPipelineStageFlags2 p_stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
        void write_image(std::string_view p_name, VkImageLayout p_layout, VkPipelineStageFlags2 p_stage, VkAccessFlags2 p_access);
        void color_attachment(std::string_view p_name, VkAttachmentLoadOp p_load, VkClearValue p_clear = {});
        void depth_attachment_read(std::string_view p_name, VkAttachmentLoadOp p_load);
        void depth_attachment(std::string_view p_name, VkAttachmentLoadOp p_load, VkClearValue p_clear = {});

        void create_buffer(std::string_view p_name, const drivers::DeviceDriverVulkan::BufferCreateInfo& p_create_info);
        void read_buffer(std::string_view p_name, VkPipelineStageFlags2 p_stage, VkAccessFlags2 p_access);
        void write_buffer(std::string_view p_name, VkPipelineStageFlags2 p_stage, VkAccessFlags2 p_access);
    };

    struct Pass {
        std::string name;
        std::string category;

        bool never_cull = false;
        std::function<void(Builder&)> setup;
        std::function<void(CommandList&)> execute;
    };

    struct Node {
        Pass* pass = nullptr;
        std::vector<ImageAccess> image_accesses;
        std::vector<ImageBarrier> pre_image_barriers;
        std::vector<BufferAccess> buffer_accesses;
        std::vector<BufferBarrier> pre_buffer_barriers;
        std::vector<int> deps;
        bool culled = false;

        bool has_render_pass = false;
        VkRenderPass render_pass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkExtent2D area{};
        std::vector<VkClearValue> clear_values;
        std::vector<int> attachment_access_idx;
    };

    std::vector<Node> nodes;
    
    /***************/
    /**** CACHE ****/
    /***************/

    std::unordered_map<uint64_t, VkRenderPass> render_pass_cache;

    VkRenderPass _get_or_create_render_pass(Node& node);
    VkRenderPass acquire_render_pass(Pass& p_pass);

    std::unordered_map<uint64_t, VkFramebuffer> framebuffer_cache;

    VkFramebuffer _get_or_create_framebuffer(Node& node);
    
    /***************/
    /**** GRAPH ****/
    /***************/

    void begin(uint32_t p_current_frame);
    void add(Pass* p_pass);
    Error compile();
    void execute(VkCommandBuffer p_cmd);
};
    
}