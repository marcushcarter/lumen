#pragma once
#include <drivers/vulkan/device_driver_vulkan.h>
#include <core/base/error.h>
#include <core/base/id_map.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace lumen {

struct RenderGraphProfiler
{
    static constexpr uint32_t INVALID = UINT32_MAX;
    static constexpr uint32_t CAPACITY = 8192;
    static constexpr uint32_t RESET_MIN = 64;

    /***************/
    /**** SETUP ****/
    /***************/
    
    drivers::DeviceDriverVulkan* dd = nullptr;
    
    bool enabled = false;
    bool stats_enabled = false;
    bool frozen = false;

    Error initialize(drivers::DeviceDriverVulkan& r_dd, uint32_t p_frame_count);
    void shutdown();

    /***************/
    /**** NAMES ****/
    /***************/
    
    std::unordered_map<uint64_t, std::string> name_table;
    IdMap _named_ids;
    
    static uint64_t intern(std::string_view p_s);
    uint64_t intern_named(std::string_view p_s);
    static uint64_t draw_key(uint64_t p_pass_name_id, uint64_t p_name_id, uint32_t p_occurrence);
    const char* name_of(uint64_t p_id) const;

    /*****************/
    /**** RESULTS ****/
    /*****************/

    enum class MarkKind : uint8_t { Pass = 0, Draw = 1, Barrier = 2, Dispatch = 3, Transfer = 4 };

    struct Timing {
        uint64_t key = 0;
        const char* name = "";
        const char* type = "";
        const char* category = "";
        double gap_ms = 0.0;
        double gpu_ms = 0.0;
        double raw_gap_ms = 0.0;
        double raw_ms = 0.0;
        uint32_t ordinal = 0;
        uint32_t parent = INVALID;
        MarkKind kind = MarkKind::Pass;
        uint32_t draw_count = 0;

        uint64_t vertices = 0;
        uint64_t primitives = 0;
        uint64_t samples = 0;
        uint32_t instances = 0;
    };

    std::vector<Timing> results;
    double total_ms = 0.0;
    uint32_t total_draws = 0;
    bool truncated = false;

    struct Smoothed { double gap = 0.0; double dur = 0.0; };
    std::unordered_map<uint64_t, Smoothed> smoothed;
    double smoothing = 0.001;

    /*******************/
    /**** RECORDING ****/
    /*******************/

    struct Mark {
        uint64_t key = 0;
        uint64_t name_id = 0;
        uint64_t type_id = 0;
        uint64_t cat_id = 0;
        uint32_t lead_query = 0;
        uint32_t begin_query = INVALID;
        uint32_t end_query = INVALID;
        uint32_t draw_count = 0;
        uint32_t ordinal = 0;
        uint32_t parent = INVALID;
        MarkKind kind = MarkKind::Pass;
        
        uint32_t instances  = 0;
        uint32_t stat_query = INVALID;
        uint32_t occl_query = INVALID;
    };

    struct Slot {
        drivers::DeviceDriverVulkan::QueryPool pool;
        std::vector<Mark> marks;
        uint32_t query_count = 0;
        uint32_t reset_count = 0;
        uint32_t last_boundary = 0;
        uint32_t open_pass = INVALID;
        uint32_t open_draw = INVALID;
        uint32_t open_barrier = INVALID;
        uint32_t pass_ordinal = 0;
        // std::unordered_map<uint64_t, uint32_t> name_occurrence;
        IdMap name_occurrence;
        bool recorded = false;
        bool overflowed = false;
        
        drivers::DeviceDriverVulkan::QueryPool stat_pool;
        drivers::DeviceDriverVulkan::QueryPool occl_pool;
        uint32_t stat_count = 0;
        uint32_t occl_count = 0;
        uint32_t stat_reset = 0;
        bool stats_recorded = false;
    };
    
    std::vector<Slot> slots;
    uint32_t slot = 0;
    double period_ns = 1.0;
    uint64_t valid_mask = ~0ull;
    
    bool supported = false;
    bool active = false;
    bool prev_active = false;
    uint32_t last_query_count = 0;
    std::vector<uint64_t> raw_scratch;

    static constexpr uint32_t STAT_COUNT = 2;
    bool stats_supported = false;
    bool stats_active = false;
    uint32_t last_stat_count = 0;
    std::vector<uint64_t> stat_scratch;
    std::vector<uint64_t> occl_scratch;

    void _clear_results();
    bool _write_boundary(VkCommandBuffer p_cmd, uint32_t& r_index);
    void _resolve();

    void frame_begin(VkCommandBuffer p_cmd, uint32_t p_slot);
    void frame_end();
    
    void pass_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_category);
    void pass_end(VkCommandBuffer p_cmd, uint32_t p_draw_count);
    void sync_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_category);
    void sync_end(VkCommandBuffer p_cmd);
    
    void _leaf_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_type, MarkKind p_kind, uint32_t p_instances, bool p_query_stats);
    void _leaf_end(VkCommandBuffer p_cmd);
    void draw_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_type, uint32_t p_instances = 1);
    void draw_end(VkCommandBuffer p_cmd);
    void dispatch_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_type = "Dispatch");
    void dispatch_end(VkCommandBuffer p_cmd);
    void transfer_begin(VkCommandBuffer p_cmd, std::string_view p_name, std::string_view p_type = "Transfer");
    void transfer_end(VkCommandBuffer p_cmd);

};

}
