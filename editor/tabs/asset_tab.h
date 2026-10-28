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
    static constexpr float PREVIEW_TIME_OF_DAY = 12.0f;

    std::filesystem::path path;
    EditorLayout layout;
    OrbitCamera orbit;
    World preview;
    Guid _preview_model;
    std::string _title;

    virtual const char* _icon() const = 0;
    bool _resolve_path(EditorContext& ctx);
    void _create_layout(EditorContext& ctx, const char* p_host_prefix);
    void _create_preview();
    void _update_orbit();
    void _refresh_title();

    const Camera* camera() const override { return &orbit.camera; }
    const World* scene() const override { return &preview; }
    void destroy(EditorContext& ctx) override;
    void on_tick(EditorContext& ctx) override;
};

}
