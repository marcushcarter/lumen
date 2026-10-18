#pragma once
#include <editor/tabs/editor_tab.h>
#include <core/assets/lmodel.h>
#include <filesystem>
#include <string>

namespace lumen {

struct ModelTab : EditorTab
{
    static constexpr TabKind KIND = TabKind::MODEL;

    LModel model;
    LModel saved;
    std::filesystem::path path;
    std::string _title;

    const char* title() const override { return _title.empty() ? "Model" : _title.c_str(); }
    bool dirty() const override { return !(model.mesh == saved.mesh); }

    Error create(EditorContext& ctx) override;
    Error save(EditorContext& ctx) override;
    void on_tick(EditorContext& ctx) override;
    void on_update(EditorContext& ctx, float p_dt) override;

    void _refresh_title();
};

}