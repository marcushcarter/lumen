#include <editor/docking/center_view/debug_tabs/profiler_tab.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <imgui.h>
#include <implot.h>

namespace lumen {

void ProfilerDebugTab::_draw_info(EditorContext& ctx, bool frozen)
{
    if (!selected_draw) return;

    RenderGraphProfiler& prof = ctx.renderer->graph.profiler;

    if (selected_draw->key != plot_key) { plot_key = selected_draw->key; plot_hist.clear(); }
    if (!frozen) plot_hist.add(static_cast<float>(selected_draw->raw_ms * 1000.0));

    imgui_title("%s · %s", selected_draw->name, selected_draw->type);

    const int n = plot_hist.data.Size;
    double avg = 0.0, median = 0.0, tmin = 0.0, tmax = 0.0;
    if (n > 0) {
        double sum = 0.0;
        tmin = tmax = plot_hist.data[0];
        for (int i = 0; i < n; ++i) {
            const float v = plot_hist.data[i];
            sum += v;
            tmin = std::min(tmin, (double)v);
            tmax = std::max(tmax, (double)v);
        }
        avg = sum / n;
        sort_scratch.assign(plot_hist.data.Data, plot_hist.data.Data + n);
        std::sort(sort_scratch.begin(), sort_scratch.end());
        median = (n & 1) ? sort_scratch[n / 2] : 0.5 * (sort_scratch[n / 2 - 1] + sort_scratch[n / 2]);
    }

    const std::vector<RenderGraphProfiler::Timing>& results = prof.results;
    int occ = 0;
    double total_us = 0.0;
    if (selected_draw->parent != RenderGraphProfiler::INVALID && selected_draw->parent < results.size()) {
        const char* sel_pass = results[selected_draw->parent].name;
        for (const RenderGraphProfiler::Timing& t : results) {
            if (t.kind != RenderGraphProfiler::MarkKind::DRAW) continue;
            if (t.parent == RenderGraphProfiler::INVALID || t.parent >= results.size()) continue;
            if (std::strcmp(results[t.parent].name, sel_pass) != 0) continue;
            if (std::strcmp(t.name, selected_draw->name) != 0) continue;
            ++occ;
            total_us += t.gpu_ms * 1000.0;
        }
    }

    imgui_property_row_value_aligned("Time Avg", "%.0f µs", avg);
    imgui_property_row_value_aligned("Time Median", "%.0f µs", median);
    imgui_property_row_value_aligned("Time Min", "%.0f µs", tmin);
    imgui_property_row_value_aligned("Time Max", "%.0f µs", tmax);
    imgui_property_row_value_aligned("Total time this frame", "%.0f µs", total_us);
    imgui_property_row_value_aligned("Occurrences this frame", "%d", occ);
    imgui_spacing();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float width  = avail.x;
    const float height = width * 2.0f / 3.0f;
    const double y_max = (tmax > 0.0) ? tmax : 1.0;
    double ticks[2] = { 0.0, y_max };

    imgui_title("Time distribution");
    ImPlot::PushStyleColor(ImPlotCol_FrameBg, ImVec4(0, 0, 0, 0));
    if (ImPlot::BeginPlot("##dist", ImVec2(width, height), ImPlotFlags_NoInputs | ImPlotFlags_NoLegend | ImPlotFlags_NoMenus | ImPlotFlags_NoTitle | ImPlotFlags_NoMouseText)) {
        ImPlot::SetupAxis(ImAxis_X1, nullptr, ImPlotAxisFlags_NoDecorations | ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, y_max, ImPlotCond_Always);
        ImPlot::PlotLine("µs", plot_hist.data.Data, plot_hist.data.Size, 1.0, 0.0);
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleColor();
}

void ProfilerDebugTab::_draw_timeline(EditorContext& ctx)
{
    RenderGraphProfiler& prof = ctx.renderer->graph.profiler;
    
    ImGuiIO& io = ImGui::GetIO();

    if (!io.WantTextInput && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) prof.frozen = true;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) prof.frozen = false;
    }

    const std::vector<RenderGraphProfiler::Timing>& src = prof.results;
    const float total_ms = (float)prof.total_ms;

    selected_pass = nullptr;
    selected_draw = nullptr;
    for (const RenderGraphProfiler::Timing& t : src) {
        if (sel_pass_key && t.kind == RenderGraphProfiler::MarkKind::PASS && t.key == sel_pass_key) selected_pass = &t;
        if (sel_draw_key && t.kind == RenderGraphProfiler::MarkKind::DRAW && t.key == sel_draw_key) selected_draw = &t;
    }
    
    ImVec2 avail = ImGui::GetContentRegionAvail();

    if (src.empty() || total_ms <= 0.0f) {
        const char* text = "No GPU timing yet.";
        ImVec2 textSize = ImGui::CalcTextSize(text);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - textSize.x) * 0.5f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (avail.y - textSize.y) * 0.5f);
        ImGui::TextDisabled("%s", text);
    } else {
        const size_t n = src.size();
        static std::vector<float> start;
        static std::vector<float> cursor;
        start.assign(n, 0.0f);
        cursor.assign(n, 0.0f);

        float frame_cursor = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const RenderGraphProfiler::Timing& t = src[i];
            if (t.kind == RenderGraphProfiler::MarkKind::PASS || t.parent == RenderGraphProfiler::INVALID) {
                start[i] = frame_cursor + (float)t.gap_ms;
                frame_cursor = start[i] + (float)t.gpu_ms;
            } else {
                const uint32_t p = t.parent;
                start[i] = cursor[p] + (float)t.gap_ms;
                cursor[p] = start[i] + (float)t.gpu_ms;
            }
            cursor[i] = start[i];
        }

        float canvas_w = avail.x;
        float canvas_h = avail.y;

        
        constexpr float MIN_RANGE_MS = 0.01f;

        static float visible_start = 0.0f;
        static float visible_range = -1.0f;

        static bool selecting = false;
        static float select_start_ms = 0.0f;
        static float select_end_ms = 0.0f;
        static bool panning = false;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        bool hovered = false;

        float range_ms = (visible_range > 0.0f) ? visible_range : total_ms;
        range_ms = std::clamp(range_ms, MIN_RANGE_MS, total_ms);

        float max_start = std::max(0.0f, total_ms - range_ms);
        visible_start = std::clamp(visible_start, 0.0f, max_start);
        float visible_end = visible_start + range_ms;

        float px = canvas_w / range_ms;
        float content_w = canvas_w;

        float H = canvas_h;
        ImGui::InvisibleButton("TimelineInput", ImVec2(content_w, H), ImGuiButtonFlags_MouseButtonLeft);
        hovered = ImGui::IsItemHovered();

        dl->PushClipRect(origin, ImVec2(origin.x + canvas_w, origin.y + H), true);

        if (hovered && io.MouseWheel != 0.0f) {
            float cursor_ms = visible_start + (io.MousePos.x - origin.x) / px;
            float cursor_frac = (io.MousePos.x - origin.x) / canvas_w;
            float new_range = range_ms * powf(0.85f, io.MouseWheel);
            new_range = std::clamp(new_range, MIN_RANGE_MS, total_ms);
            visible_start = cursor_ms - cursor_frac * new_range;
            visible_range = new_range;
            follow = false;
        }

        if (hovered && io.MouseWheelH != 0.0f && visible_range > 0.0f) {
            visible_start -= io.MouseWheelH * range_ms * 0.1f;
            follow = false;
        }

        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            follow = false;
            visible_range = -1.0f;
            visible_start = 0.0f;
            selecting = false;
            panning = false;
        } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (io.KeyAlt) {
                panning = true;
            } else {
                selecting = true;
                select_start_ms = visible_start + (io.MousePos.x - origin.x) / px;
                select_end_ms   = select_start_ms;
            }
        }
        
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) panning = true;

        if (panning) {
            bool held = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
            if (held) visible_start -= io.MouseDelta.x / px;
            else panning = false;
        }

        if (hovered && ImGui::IsKeyPressed(ImGuiKey_F) && sel_name[0]) {
            follow = !follow;
            if (follow && visible_range <= 0.0f) visible_range = range_ms;
        }

        if (follow && sel_name[0]) {
            for (size_t i = 0; i < n; ++i) {
                if (src[i].kind != RenderGraphProfiler::MarkKind::PASS) continue;
                if (std::strcmp(src[i].name ? src[i].name : "", sel_name) != 0) continue;
                float target_start = start[i];
                float target_range = std::max((float)src[i].gpu_ms, MIN_RANGE_MS);

                const float k = 0.2f;
                visible_start += (target_start - visible_start) * k;
                visible_range += (target_range - visible_range) * k;
                break;
            }
        }

        if (selecting) {
            select_end_ms = visible_start + (io.MousePos.x - origin.x) / px;
            select_start_ms = std::clamp(select_start_ms, 0.0f, total_ms);
            select_end_ms = std::clamp(select_end_ms, 0.0f, total_ms);
        }

        if (selecting && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            selecting = false;
            float a = std::clamp(std::min(select_start_ms, select_end_ms), 0.0f, total_ms);
            float b = std::clamp(std::max(select_start_ms, select_end_ms), 0.0f, total_ms);
            if (b - a > 0.01f) {
                visible_start = a;
                visible_range = b - a;
                follow = false;
            }
        }

        range_ms = (visible_range > 0.0f) ? visible_range : total_ms;
        range_ms = std::clamp(range_ms, MIN_RANGE_MS, total_ms);
        max_start = std::max(0.0f, total_ms - range_ms);
        visible_start = std::clamp(visible_start, 0.0f, max_start);
        visible_end = visible_start + range_ms;
        px = canvas_w / range_ms;

        // Grid.
        {
            float visible_ms = canvas_w / px;
            float start_ms = visible_start;

            float block_ms;
            if (visible_ms >= 1.5f) block_ms = 1.0f;
            else if (visible_ms >= 0.15f) block_ms = 0.1f;
            else block_ms = 0.01f;

            int first_index = (int)floorf(start_ms / block_ms);
            float end = visible_end;

            for (int i = first_index; ; i++) {
                float t = i * block_ms;
                if (t > end) break;

                float x0 = origin.x + (t - visible_start) * px;
                float x1 = origin.x + (t + block_ms - visible_start) * px;

                bool dark = (i & 1) == 0;
                dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x1, origin.y + H), dark ? IM_COL32(45, 45, 45, 255) : IM_COL32(55, 55, 55, 255));

                char label[32];
                if (block_ms == 1.0f) snprintf(label, sizeof(label), "%.0f ms", t);
                else if (block_ms == 0.1f) snprintf(label, sizeof(label), "%.1f ms", t);
                else snprintf(label, sizeof(label), "%.2f ms", t);

                dl->AddText(ImVec2(x0 + 3.0f, origin.y + H - ImGui::GetTextLineHeight()), IM_COL32(220, 220, 220, 180), label);
            }
        }

        const float gap = 3.0f;
        const float sec_h = 18.0f;
        const float pass_h = 22.0f;
        const float barr_h = 8.0f;
        const float draw_h = 14.0f;

        float y_sec0  = origin.y;
        float y_sec1 = y_sec0 + sec_h;
        float y_pass0 = y_sec1 + gap;
        float y_pass1 = y_pass0 + pass_h;
        float y_barr0 = y_sec1 + gap;
        float y_barr1 = y_pass0 + barr_h;
        float y_draw0 = y_pass1 + gap;
        float y_draw1 = y_draw0 + draw_h;

        auto label_in = [&](ImVec2 a, ImVec2 b, const char* txt, ImU32 col) {
            if (!txt || !txt[0]) return;
            float w = b.x - a.x;
            if (w < 6.0f) return;
            ImVec2 ts = ImGui::CalcTextSize(txt);
            float x = a.x + 4.0f;
            x = std::max(x, origin.x + 4.0f);
            if (x + ts.x > b.x - 4.0f) ts.x = b.x - x - 4.0f;
            dl->PushClipRect(a, b, true);
            ImVec2 p(x, a.y + ((b.y - a.y) - ts.y) * 0.5f);
            dl->AddText(ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 140), txt);
            dl->AddText(p, col, txt);
            dl->PopClipRect();
        };

        bool bar_click_consumed = false;
        bool dragging = panning || ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle);

        auto bar = [&](float ms_start, float ms_len, float y0, float y1, ImU32 fill, bool* r_hovered = nullptr) -> ImVec2 {
            float x0 = origin.x + (ms_start - visible_start) * px;
            float x1 = origin.x + (ms_start + ms_len - visible_start) * px;
            ImVec2 a(x0 + 1.0f, y0);
            ImVec2 b(x1 - 1.0f, y1);
            const bool in_canvas = io.MousePos.x >= origin.x && io.MousePos.x <= origin.x + canvas_w && io.MousePos.y >= origin.y && io.MousePos.y <= origin.y + H;
            bool is_hovered = in_canvas && !dragging && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y;
            if (r_hovered) *r_hovered = is_hovered;
            if (b.x > a.x) dl->AddRectFilled(a, b, (r_hovered && is_hovered) ? IM_COL32(255, 50, 50, 255) : fill, 3.0f);
            return ImVec2(a.x, b.x);
        };

        // Categories.
        {
            float x_ms = 0.0f;
            const char* run_cat = nullptr;
            float run_ms = 0.0f;

            auto flush = [&]() {
                if (!run_cat) return;
                ImVec2 x = bar(x_ms, run_ms, y_sec0, y_sec1, imgui_rg_category_u32(run_cat));
                label_in(ImVec2(x.x, y_sec0), ImVec2(x.y, y_sec1), run_cat[0] ? run_cat : "(uncat)", IM_COL32_WHITE);
                x_ms += run_ms;
                run_ms = 0.0f;
            };

            for (const RenderGraphProfiler::Timing& t : src) {
                if (t.kind != RenderGraphProfiler::MarkKind::PASS && t.kind != RenderGraphProfiler::MarkKind::BARRIER) continue;
                const char* cat = t.category ? t.category : "";
                const float span = (float)(t.gap_ms + t.gpu_ms);
                if (run_cat && std::strcmp(run_cat, cat) == 0) { run_ms += span; continue; }
                flush();
                run_cat = cat;
                run_ms = span;
            }

            flush();
        }

        // Passes.
        for (size_t i = 0; i < n; ++i) {
            const RenderGraphProfiler::Timing& t = src[i];
            if (t.kind != RenderGraphProfiler::MarkKind::PASS) continue;

            bool bar_hovered = false;
            ImVec2 x = bar(start[i], (float)t.gpu_ms, y_pass0, y_pass1, imgui_rg_category_u32(t.category), &bar_hovered);
            if (bar_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                bar_click_consumed = true;
                if (sel_pass_key == t.key) {
                    sel_pass_key = 0;
                    selected_pass = nullptr;
                    sel_name[0] = '\0';
                    sel_draw_key = 0;
                    selected_draw = nullptr;
                } else {
                    sel_pass_key = t.key; selected_pass = &t;
                    if (selected_draw && selected_draw->parent != RenderGraphProfiler::INVALID && src[selected_draw->parent].key != t.key) {
                        sel_draw_key = 0;
                        selected_draw = nullptr;
                    }
                    snprintf(sel_name, sizeof(sel_name), "%s", t.name ? t.name : "");
                }
            }
            if (&t == selected_pass && x.y > x.x) dl->AddRect(ImVec2(x.x, y_pass0), ImVec2(x.y, y_pass1), IM_COL32_WHITE, 3.0f, 0, 1.0f);
            label_in(ImVec2(x.x, y_pass0), ImVec2(x.y, y_pass1), t.name, IM_COL32_WHITE);

            if (bar_hovered) {
                ImGui::BeginTooltip();
                
                imgui_title("%s", t.name);
                imgui_property_row("Time", "%.0f µs", t.gpu_ms*1000.0f);
                imgui_property_row("Pixel Count", "%llu", (unsigned long long)t.samples);

                ImGui::EndTooltip();
            }
        }

        // Draw calls + compute dispatches.
        for (size_t i = 0; i < n; ++i) {
            const RenderGraphProfiler::Timing& t = src[i];
            if (t.kind != RenderGraphProfiler::MarkKind::DRAW && t.kind != RenderGraphProfiler::MarkKind::DISPATCH && t.kind != RenderGraphProfiler::MarkKind::TRANSFER) continue;

            const uint32_t p = t.parent;
            if (p == RenderGraphProfiler::INVALID) continue;
            const float parent_end = start[p] + (float)src[p].gpu_ms;
            const float a_ms = start[i];
            if (a_ms >= parent_end) continue;
            const float len_ms = std::min((float)t.gpu_ms, parent_end - a_ms);

            bool bar_hovered = false;
            ImVec2 x = bar(a_ms, len_ms, y_draw0, y_draw1, imgui_rg_category_u32(src[p].category, 0.45f), &bar_hovered);
            if (bar_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                bar_click_consumed = true;
                if (sel_draw_key == t.key) {
                    sel_draw_key = 0; selected_draw = nullptr;
                } else {
                    sel_draw_key = t.key; selected_draw = &t;
                    const RenderGraphProfiler::Timing& parent = src[p];
                    sel_pass_key = parent.key; selected_pass = &parent;
                    snprintf(sel_name, sizeof(sel_name), "%s", parent.name ? parent.name : "");
                }
            }
            if (&t == selected_draw && x.y > x.x) dl->AddRect(ImVec2(x.x, y_draw0), ImVec2(x.y, y_draw1), IM_COL32_WHITE, 3.0f, 0, 1.0f);

            const bool named = t.name && t.name[0];
            char lbl[16];
            if (!named) snprintf(lbl, sizeof(lbl), "%u", t.ordinal);
            label_in(ImVec2(x.x, y_draw0), ImVec2(x.y, y_draw1), named ? t.name : lbl, IM_COL32_WHITE);

            if (bar_hovered) {
                ImGui::BeginTooltip();

                const bool is_graphics = t.kind == RenderGraphProfiler::MarkKind::DRAW;
                if (named) {
                    imgui_title("%s · %s", t.name, t.type);
                } else {
                    const char* k = t.kind == RenderGraphProfiler::MarkKind::DISPATCH ? "Dispatch" : t.kind == RenderGraphProfiler::MarkKind::TRANSFER ? "Transfer" : "Draw";
                    imgui_title("%s %u", k, t.ordinal);
                }
                imgui_property_row("Time", "%.0f µs", t.gpu_ms*1000.0);
                if (is_graphics) imgui_property_row("Pixel Count", "%llu", (unsigned long long)t.samples);
                imgui_spacing();
                
                imgui_title("Owner Object");
                imgui_property_row("Pass", "%s", src[p].name);
                imgui_property_row("Draw Index", "%u", t.ordinal);
                imgui_property_row("Name", "%s", t.name);
                imgui_property_row("Location", "%s", "n/a");
                imgui_property_row("Type", "%s", t.type);
                imgui_spacing();

                if (is_graphics) {
                    imgui_title("Primitives");
                    imgui_property_row("Triangles", "%llu", (unsigned long long)t.primitives);
                    imgui_property_row("Vertices", "%llu", (unsigned long long)t.vertices);
                    imgui_property_row("Instances", "%u", t.instances);
                    imgui_property_row("Total Triangles", "%llu", (unsigned long long)src[p].primitives);
                }
                
                ImGui::EndTooltip();
            }
        }

        // Barriers.
        for (size_t i = 0; i < n; ++i) {
            const RenderGraphProfiler::Timing& t = src[i];
            if (t.kind != RenderGraphProfiler::MarkKind::BARRIER) continue;
            if (t.gpu_ms <= 0.0) continue;

            bool bar_hovered = false;
            bar(start[i], (float)t.gpu_ms, y_barr0, y_barr1, IM_COL32(90, 130, 180, 220), &bar_hovered);

            if (bar_hovered) {
                ImGui::BeginTooltip();
                imgui_title("Syncronization · before %s", t.name);
                imgui_property_row("Time", "%.0f µs", t.gpu_ms * 1000.0);
                imgui_property_row("Gap", "%.0f µs", t.gap_ms * 1000.0);
                ImGui::EndTooltip();
            }
        }

        // Highlight.
        if (selecting) {
            float a = std::min(select_start_ms, select_end_ms);
            float b = std::max(select_start_ms, select_end_ms);
            dl->AddRectFilled(ImVec2(origin.x + (a - visible_start) * px, origin.y), ImVec2(origin.x + (b - visible_start) * px, origin.y + H), IM_COL32(100, 150, 255, 50));
        }

        // Hover bar.
        if (hovered) {
            dl->AddLine(ImVec2(io.MousePos.x, origin.y), ImVec2(io.MousePos.x, origin.y + H), IM_COL32(255, 255, 255, 120), 1.0f);
        }

        // Live / Frozen status.
        {
            const char* status = prof.frozen ? "Frozen" : "Live View";
            ImVec2 ts = ImGui::CalcTextSize(status);
            float tx = origin.x + 4.0f;
            float ty = origin.y + H - ts.y - 2.0f;
            dl->AddRectFilled(ImVec2(tx - 2.0f, ty - 1.0f), ImVec2(tx + ts.x + 2.0f, ty + ts.y + 1.0f), IM_COL32(20, 20, 20, 180));
            dl->AddText(ImVec2(tx, ty), ImGui::GetColorU32(ImGuiCol_TextDisabled), status);
        }

        if (hovered && !io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !bar_click_consumed) {
            sel_pass_key = 0;
            selected_pass = nullptr;
            sel_name[0] = '\0';
            sel_draw_key = 0;
            selected_draw = nullptr;
        }

        dl->PopClipRect();
    }
}

void ProfilerDebugTab::draw(EditorContext& ctx)
{
    auto& profiler = ctx.renderer->graph.profiler;

    ImVec2 avail = ImGui::GetContentRegionAvail();

    const float right_width = avail.x * 0.2f;
    const float leftWidth = avail.x - right_width - ImGui::GetStyle().ItemSpacing.x;

    ImGui::BeginGroup();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("TopLeft", ImVec2(leftWidth, avail.y), ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();
    _draw_timeline(ctx);
    ImGui::EndChild();

    ImGui::EndGroup();
    ImGui::SameLine();
    
    ImGui::BeginChild("Right", ImVec2(right_width, avail.y), ImGuiChildFlags_Borders);
    {
        ProfilingSettings& prof = *ctx.profiling;
        ImGui::BeginDisabled(!profiler.supported);
        ImGui::Checkbox("Enable GPU Profiling", &prof.gpu);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!prof.gpu || !profiler.stats_supported);
        ImGui::Checkbox("Enable Pipeline Statistics", &prof.gpu_pipeline_stats);
        ImGui::EndDisabled();


        imgui_title("Legend");
        imgui_property_row_value_aligned("Pan Area", "ALT + Mouse");
        imgui_property_row_value_aligned("Navigate", "Mouse Scroll");
        imgui_property_row_value_aligned("Zoom Area", "Mouse Drag");
        imgui_property_row_value_aligned("Zoom Out", "Double Click");
        imgui_property_row_value_aligned("Frame Pass", "F");
        imgui_property_row_value_aligned("Record", "Space");
        imgui_property_row_value_aligned("Resume", "ESC");
        imgui_spacing();

        _draw_info(ctx, profiler.frozen);
    }
    ImGui::EndChild();
}

}