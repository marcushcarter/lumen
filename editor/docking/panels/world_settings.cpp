#include <editor/docking/panels/world_settings.h>
#include <core/project/project.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <editor/editor_context.h>
#include <core/base/profiling.h>
#include <imgui.h>

namespace lumen {
    
void WorldSettingsPanel::draw_contents(EditorContext& ctx)
{
    ImGui::Text("World Settings");
    
    ImGui::BeginDisabled(true);
    ImGui::DragInt("Window width", &ctx.project->settings.width);
    ImGui::SameLine();
    if (ImGui::Button("Reset##Width")) ctx.project->settings.width = 1280;
    ImGui::DragInt("Window height", &ctx.project->settings.height);
    ImGui::SameLine();
    if (ImGui::Button("Reset##Height")) ctx.project->settings.height = 720;
    ImGui::EndDisabled();

    ImGui::DragFloat("LOD Bias", &ctx.renderer->lod_bias, 0.01f, 0.01f, 1.f);
    ImGui::SameLine();
    if (ImGui::Button("Reset##LOD Bias")) ctx.renderer->lod_bias = 0.6f;

    if (!ctx.render_path) return;
    GeometryFeature& geo = ctx.render_path->geometry;
    const GeometryFeature::CullStats& s = geo.stats;
    const uint32_t occluded = s.retest - s.phase2_visible;

    ImGui::Text("HiZ: %s", !geo.occlusion ? "off" : !geo.hiz_ok ? "unavailable" : geo.hiz_use_prev ? "active" : "warming up");
    if (!ctx.profiling || !ctx.profiling->cull_stats_on()) {
        ImGui::TextDisabled("Enable pipeline statistics in the GPU profiler to see counts.");
        return;
    }
    ImGui::Text("After LOD + frustum: %u", s.refs);
    ImGui::Text("Phase 1 drawn: %u", s.phase1_visible);
    ImGui::Text("Phase 1 deferred: %u", s.retest);
    ImGui::Text("Phase 2 drawn: %u", s.phase2_visible);
    ImGui::Text("Occluded: %u (%.1f%%)", occluded, s.refs ? 100.0f * (float)occluded / (float)s.refs : 0.0f);
}

}