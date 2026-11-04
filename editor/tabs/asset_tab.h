#pragma once
#include <core/world/world.h>
#include <editor/layout/editor_layout.h>
#include <editor/tabs/editor_tab.h>
#include <editor/viewport/orbit_camera.h>
#include <filesystem>
#include <string>

namespace lumen {

struct AssetTab : EditorTab
{
    static constexpr double INFO_POLL_S = 1.0;

    std::filesystem::path path;
    EditorLayout layout;
    OrbitCamera orbit;
    World preview;
    Guid _preview_model;
    Entity _preview_entity;
    std::string _title;

    std::string source;
    uint64_t disk_size = 0;
    std::string _tooltip;
    std::filesystem::file_time_type _info_time{};
    double _info_poll = -1.0e9;

    virtual const char* _icon() const = 0;
    bool _resolve_path(EditorContext& ctx);
    void _refresh_info(EditorContext& ctx);
    void _create_layout(EditorContext& ctx, const char* p_host_prefix);
    void _create_preview();
    void _update_orbit();
    void _refresh_title();
    void _draw_layout(EditorContext& ctx);
    virtual void _draw_toolbar(EditorContext& ctx);
    void draw_import() const;

    const char* tooltip() const override { return _tooltip.empty() ? nullptr : _tooltip.c_str(); }

    const Camera* camera() const override { return &orbit.camera; }
    const World* scene() const override { return &preview; }
    void destroy(EditorContext& ctx) override;
    void on_tick(EditorContext& ctx) override;
};

}
