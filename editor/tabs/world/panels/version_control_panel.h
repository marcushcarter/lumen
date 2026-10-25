#pragma once
#include <editor/layout/panel.h>
#include <drivers/git/git_driver.h>
#include <IconsFontAwesome6.h>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen {

struct VersionControlPanel : Panel
{
    static constexpr uint32_t MESSAGE_SIZE = 2048;
    static constexpr uint32_t FIELD_SIZE = 256;
    static constexpr uint32_t CHANGES_TOOLTIP_MAX = 40;
    static constexpr int32_t NONE = -1;
    static constexpr double POLL_INTERVAL = 2.0;
    static constexpr const char* GIT_DOWNLOAD_URL = "https://git-scm.com/download/win";

    struct Group {
        std::string name;
        ImU32 color;
        uint32_t lane;
        uint32_t first_row;
        uint32_t last_row;
        uint32_t top;
        uint32_t bottom;
        bool branch;
        bool uploaded;
    };

    struct Edge {
        uint32_t child;
        uint32_t parent;
        bool merge;
    };

    drivers::GitDriver::Status cached;
    uint64_t built_version = UINT64_MAX;

    std::vector<int32_t> row_group;
    std::vector<uint8_t> row_unuploaded;
    std::vector<Group> groups;
    std::vector<Edge> edges;
    uint32_t lane_count = 1;
    int32_t head_row = NONE;
    int32_t main_group = NONE;

    int32_t selected = NONE;
    std::string selected_hash;
    int32_t menu_row = NONE;
    bool scroll_to_selected = false;

    char message[MESSAGE_SIZE] = {};
    char remote_url[FIELD_SIZE] = {};
    char user_name[FIELD_SIZE] = {};
    char user_email[FIELD_SIZE] = {};
    char rename_buffer[FIELD_SIZE] = {};
    std::string rename_branch;
    bool rename_open = false;
    bool commit_pending = false;
    bool editing_remote = false;
    double last_poll = -1.0e9;
    bool download_pending = false;

    const char* name() const override { return ICON_FA_CODE_BRANCH "  Version Control"; }
    DockZone default_zone() const override { return DockZone::RIGHT_BOTTOM; }
    ImGuiWindowFlags window_flags() const override { return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse; }
    ImVec2 window_padding() const override { return ui_style().outliner.padding; }
    void draw_contents(EditorContext& ctx) override;

    void _sync(EditorContext& ctx);
    void _build_graph();
    void _claim(int32_t p_group, int32_t p_row, const std::vector<int32_t>& p_first_parent);
    int32_t _add_group(const std::string& p_name);
    bool _identity() const { return !cached.user_name.empty() && !cached.user_email.empty(); }
    bool _message_blank() const;
    void _commit(EditorContext& ctx);
    void _go_to(EditorContext& ctx, const std::string& p_hash);

    void _draw_setup(EditorContext& ctx);
    void _draw_toolbar(EditorContext& ctx);
    void _draw_banner(EditorContext& ctx);
    void _draw_remote(EditorContext& ctx);
    void _draw_graph(EditorContext& ctx, float p_footer_h);
    void _draw_footer();
    void _draw_commit_menu(EditorContext& ctx);
    void _draw_rename_popup(EditorContext& ctx);

    static ImU32 _branch_color(const std::string& p_name);
    static std::string _merged_name(const std::string& p_summary);
    static void _time_ago(int64_t p_time, char* r_out, size_t p_size);
};

}