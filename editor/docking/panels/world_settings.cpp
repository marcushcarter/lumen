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

    if (!ctx.render_path) return;
    GeometryFeature& geo = ctx.render_path->geometry;
    const GeometryFeature::CullStats& s = geo.stats;
    // const uint32_t occluded = s.retest - s.phase2_visible;

    ImGui::Text("HiZ: %s", !geo.occlusion ? "off" : !geo.hiz_ok ? "unavailable" : geo.hiz_use_prev ? "active" : "warming up");
    if (!ctx.profiling || !ctx.profiling->cull_stats_on()) {
        ImGui::TextDisabled("Enable pipeline statistics in the GPU profiler to see counts.");
        return;
    }



    auto draw_occlusion = [](const char* p_title, const char* p_input_label, uint32_t p_input, uint32_t p_drawn_1, uint32_t p_deferred, uint32_t p_drawn_2) {
        const uint32_t occluded = p_deferred - p_drawn_2;
        ImGui::SeparatorText(p_title);
        ImGui::Text("%s: %u", p_input_label, p_input);
        ImGui::Text("Phase 1 drawn: %u", p_drawn_1);
        ImGui::Text("Phase 1 deferred: %u", p_deferred);
        ImGui::Text("Phase 2 drawn: %u", p_drawn_2);
        ImGui::Text("Occluded: %u (%.1f%%)", occluded, p_input ? 100.0f * (float)occluded / (float)p_input : 0.0f);
    };

    draw_occlusion("Instance Occlusion", "After frustum", s.instances_visible + s.instances_occluded, s.instances_visible, s.instances_occluded, s.instances_recovered);
    draw_occlusion("Cluster Occlusion", "After LOD + frustum", s.refs, s.phase1_visible, s.retest, s.phase2_visible);


    // ImGui::SeparatorText("Instance Occlusion");
    // ImGui::Text("Instances visible (phase 1): %u", s.instances_visible);
    // ImGui::Text("Instances occluded (phase 1): %u", s.instances_occluded);
    // ImGui::Text("Instances recovered (phase 2): %u", s.instances_recovered);
    
    // ImGui::SeparatorText("Cluster Occlusion");
    // ImGui::Text("After LOD + frustum: %u", s.refs);
    // ImGui::Text("Phase 1 drawn: %u", s.phase1_visible);
    // ImGui::Text("Phase 1 deferred: %u", s.retest);
    // ImGui::Text("Phase 1 drawn: %u", s.phase1_visible);
    // ImGui::Text("Phase 2 drawn: %u", s.phase2_visible);
    // ImGui::Text("Occluded: %u (%.1f%%)", occluded, s.refs ? 100.0f * (float)occluded / (float)s.refs : 0.0f);
}

}