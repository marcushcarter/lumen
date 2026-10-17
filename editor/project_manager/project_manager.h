#pragma once
#include <editor/editor_context.h>
#include <editor/popups/project/new_project.h>
#include <editor/popups/project/delete_project.h>
#include <core/base/error.h>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace lumen {

struct ProjectManager
{
    struct ContentStats {
        uint32_t meshes = 0;
        uint32_t textures = 0;
        uint64_t disk_bytes = 0;
    };

    struct Entry {
        std::string name;
        std::filesystem::path path;
        bool favorite = false;
        std::string last_opened;
        ContentStats stats;
        bool stats_valid = false;
        bool missing = false;
    };

    enum class Filter { ALL, FAVORITES, MISSING };

    std::vector<Entry> recent;
    std::vector<int> visible;
    Filter filter = Filter::ALL;
    int selected = -1;
    int  sort_index = 0;
    char filter_buf[128] = {};

    std::future<ContentStats> stats_job;
    std::filesystem::path stats_job_path;

    bool new_project_request = false;
    char new_project_name[128] = {};
    char new_project_location[512] = {};

    Error initialize();
    void shutdown();

    void load_recents();
    void save_recents();
    void add_recent(const std::filesystem::path& p_root, std::string_view p_name);

    static ContentStats _scan_content(const std::filesystem::path& p_root);
    void _update_stats();
    void _rebuild_visible();
    void _draw_toolbar();
    void _draw_list(EditorContext& ctx);
    void _draw_inspector(EditorContext& ctx);

    void on_update(EditorContext& ctx);
};

}