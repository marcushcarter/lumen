#include <editor/tabs/model/model_tab.h>
#include <editor/tabs/tab_manager.h>
#include <editor/assets/asset_registry.h>
#include <core/base/error.h>
#include <imgui.h>

namespace lumen {

Error ModelTab::create(EditorContext& ctx)
{
    using enum Error;
    if (path.empty()) {
        const AssetRegistry::Entry* e = ctx.assets->find(guid);
        if (!e) return FAILED;
        path = e->path;
    }
    if (model.load(path) != OK || !(model.guid == guid)) {
        log_write("Model Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = model;
    _refresh_title();
    return OK;
}

Error ModelTab::save(EditorContext& ctx)
{
    (void)ctx;
    if (model.save(path) != Error::OK) {
        log_write("Model Tab: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = model;
    _refresh_title();
    return Error::OK;
}

void ModelTab::on_tick(EditorContext& ctx)
{
    if (!ctx.assets->dirty) {
        const AssetRegistry::Entry* e = ctx.assets->find(guid);
        if (!e) {
            ctx.tabs->close(KIND, guid, true);
            return;
        }
        path = e->path;
    }
    _refresh_title();
}

void ModelTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)ctx;
    (void)p_dt;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##ModelHost", nullptr,
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
    );
    ImGui::PopStyleVar(3);
    ImGui::End();
}

void ModelTab::_refresh_title()
{
    _title = path.stem().string();
    if (dirty()) _title += '*';
}

}