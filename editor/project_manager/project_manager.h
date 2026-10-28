#pragma once
#include <editor/application/editor_context.h>
#include <core/base/error.h>
#include <core/base/jobs.h>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

struct ProjectManager
{
    enum class Filter { ALL, FAVORITES, MISSING };
    enum class NewProjectStatus { VALID, WARNING, INVALID };

    struct ContentStats {
        uint32_t meshes = 0;
        uint32_t textures = 0;
        uint32_t materials = 0;
        uint32_t models = 0;
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

    struct StatsJob {
        JobCounter counter;
        std::filesystem::path path;
        ContentStats stats;
    };

    std::vector<Entry> recent;
    std::vector<int> visible;
    Filter filter = Filter::ALL;
    int selected = -1;
    int sort_index = 0;
    char filter_buf[128] = {};

    std::unique_ptr<StatsJob> stats_job;

    bool new_project_request = false;
    char new_project_name[128] = {};
    char new_project_location[512] = {};
    bool new_project_create_folder = true;
    bool new_project_git = true;
    NewProjectStatus new_project_status = NewProjectStatus::INVALID;
    std::string new_project_message;
    std::filesystem::path new_project_root;

    static ContentStats _scan_content(const std::filesystem::path& p_root);
    void _update_stats();
    void _rebuild_visible();

    Error initialize();
    void shutdown();
    void load_recents();
    void save_recents();
    void add_recent(const std::filesystem::path& p_root, std::string_view p_name);

    void _new_project_open();
    void _new_project_validate();
    void _new_project_create(EditorContext& ctx, bool p_edit);

    void _draw_toolbar();
    void _draw_list(EditorContext& ctx);
    void _draw_inspector(EditorContext& ctx);
    void _draw_new_project(EditorContext& ctx);
    void on_update(EditorContext& ctx);
};

}
