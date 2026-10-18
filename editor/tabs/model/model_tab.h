#pragma once
#include <editor/tabs/editor_tab.h>
#include <core/assets/lmodel.h>
#include <editor/layout/editor_layout.h>
#include <editor/viewport/orbit_camera.h>
#include <IconsFontAwesome6.h>
#include <filesystem>
#include <string>

namespace lumen {

struct ModelTab : EditorTab
{
    static constexpr TabKind KIND = TabKind::MODEL;
    static constexpr const char* ICON = ICON_FA_CUBES;

    LModel model;
    LModel saved;
    std::filesystem::path path;
    EditorLayout layout;
    OrbitCamera orbit;
    std::string _title;

    const char* title() const override { return _title.empty() ? "Model" : _title.c_str(); }
    bool dirty() const override { return !(model.mesh == saved.mesh); }
    const Camera* camera() const override { return &orbit.camera; }

    Error create(EditorContext& ctx) override;
    void destroy(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_tick(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;

    void _refresh_title();
};

}