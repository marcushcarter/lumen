#include <editor/docking/center_view/center_view.h>
#include <drivers/imgui/imgui_driver.h>
#include <drivers/imgui/imgui_helpers.h>
#include <core/rendering/renderer.h>
#include <core/rendering/render_graph.h>
#include <core/rendering/render_path/editor_render_path.h>
#include <core/rendering/features/debug_view.h>
#include <IconsFontAwesome6.h>
#include <imgui_internal.h>
#include <cstring>
#include <cstdio>

namespace lumen {

void CenterView::initialize()
{
    debugger.initialize();
}

static constexpr float VIEW_ITEM_W = 210.0f;

static void _view_row_decor(ImDrawList* dl, ImVec2 p, float h, const char* text, bool filled)
{
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    const float cy = p.y + h * 0.5f;
    const float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddCircle(ImVec2(p.x + 12.0f, cy), 5.0f, col, 20, 1.5f);
    if (filled) dl->AddCircleFilled(ImVec2(p.x + 12.0f, cy), 2.5f, col, 20);
    dl->AddText(ImVec2(p.x + 28.0f, ty), col, text); // name already carries its icon
}

bool CenterView::_view_item(const char* p_name, int p_id)
{
    ImGui::PushID(p_id);
    const bool sel = (selected_view == p_id);
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable("##vi", sel, 0, ImVec2(VIEW_ITEM_W, h));
    _view_row_decor(ImGui::GetWindowDrawList(), p, h, p_name, sel);
    if (clicked) selected_view = p_id;
    ImGui::PopID();
    return clicked;
}

bool CenterView::_view_submenu(const char* p_category, bool p_active)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float space_w = ImGui::CalcTextSize(" ").x;
    int n = (space_w > 0.0f) ? (int)(VIEW_ITEM_W / space_w) : 40;
    if (n > 220) n = 220;

    char id[256];
    for (int k = 0; k < n; ++k) id[k] = ' ';
    snprintf(id + n, sizeof(id) - (size_t)n, "###%s", p_category);

    const bool open = ImGui::BeginMenu(id);

    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    const float h = item_max.y - item_min.y;

    _view_row_decor(dl, item_min, h, p_category, p_active);

    return open;
}

void CenterView::_draw_scene(EditorContext& ctx)
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    if (!ImGui::IsAnyItemActive()) {
        ctx.renderer->request_size((uint32_t)(size.x * screen_percentage), (uint32_t)(size.y * screen_percentage));
    }
    
    RenderGraph::ImageResource* sel = ctx.renderer->graph.image_resource("Viewport");
    VkImageView sel_view = VK_NULL_HANDLE;
    if (sel && sel->image && sel->image->state.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) sel_view = sel->image->image_view;
    
    VkDescriptorSet set = ctx.imgui->texture_cache.get(sel_view);
    if (set) {
        ImGui::Image((ImTextureID)set, size, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    } else {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(25, 25, 25, 255));
    }

    left_overlay.begin(pos, size, OverlayBar::Align::Left);
    if (left_overlay.begin_menu(ICON_FA_BARS)) {
        ImGui::SeparatorText("VIEWPORT OPTIONS");
        ImGui::Separator();
        ImGui::SliderFloat("Screen Percentage", &screen_percentage, 0.01f, 1.0f);
        left_overlay.end_menu();
    }
    left_overlay.end();

    char view_btn[128];
    snprintf(view_btn, sizeof(view_btn), "%s###ViewMode", DEBUG_VIEWS[selected_view].name);

    right_overlay.begin(pos, size, OverlayBar::Align::Right);
    if (right_overlay.begin_menu(view_btn)) {
        int i = 0;
        while (i < DEBUG_VIEW_COUNT) {
            const DebugView& d = DEBUG_VIEWS[i];
            if (d.category[0] == '\0') { _view_item(d.name, i); i++; continue; }

            int j = i;
            bool active = false;
            while (j < DEBUG_VIEW_COUNT && strcmp(DEBUG_VIEWS[j].category, d.category) == 0) {
                if (j == selected_view) active = true;
                j++;
            }
            if (_view_submenu(d.category, active)) {
                for (int k = i; k < j; k++) _view_item(DEBUG_VIEWS[k].name, k);
                ImGui::EndMenu();
            }
            i = j;
        }
        right_overlay.end_menu();
    }
    right_overlay.end();

    if (ctx.render_path) ctx.render_path->debug.view = (uint32_t)selected_view;
}

void CenterView::draw(EditorContext& ctx)
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float strip_h = ImGui::GetFrameHeight();
    const float handle_h = 6.0f;
    float above_h = avail.y - strip_h;
    if (above_h < 1.0f) above_h = 1.0f;

    float usable = above_h - handle_h;
    if (usable < 1.0f) usable = 1.0f;

    const float min_scene = 80.0f;
    const float min_content = strip_h;

    float scene_h, content_h;
    if (debugger.collapsed) {
        scene_h = usable;
        content_h = 0.0f;
    } else {
        float min_r = min_scene / usable;
        float max_r = 1.0f - (min_content / usable);
        if (max_r < min_r) max_r = min_r;
        split_ratio = ImClamp(split_ratio, min_r, max_r);
        scene_h = ImFloor(usable * split_ratio);
        content_h = usable - scene_h;
    }
    
    ImGui::BeginChild("##top", ImVec2(avail.x, scene_h), false, ImGuiWindowFlags_NoScrollbar);
    _draw_scene(ctx);
    ImGui::EndChild();
    
    SplitterState s = imgui_splitter("##vsplit", SplitAxis::Y, ImVec2(avail.x, handle_h));
    if (s.active) {
        if (debugger.collapsed && s.activated) split_ratio = 1.0f;
        split_ratio += s.delta / usable;
        debugger.collapsed = (usable * (1.0f - split_ratio) < min_content);
    }
    
    ImVec2 bmin = ImGui::GetItemRectMin();
    ImVec2 bmax = ImGui::GetItemRectMax();
    float cy = ImFloor((bmin.y + bmax.y) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    
    const float grip_w = 40.0f;
    float gx = ImFloor((bmin.x + bmax.x) * 0.5f);
    ImU32 grip_col = ImGui::GetColorU32(s.active ? ImGuiCol_SeparatorActive : s.hovered ? ImGuiCol_SeparatorHovered : ImGuiCol_Separator);
    dl->AddRectFilled(ImVec2(gx - grip_w * 0.5f, cy - 2.0f), ImVec2(gx + grip_w * 0.5f, cy + 2.0f), grip_col, 2.0f);

    if (!debugger.collapsed) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8)); // child padding, killed by the layout's 0-wrap
        ImGui::BeginChild("##bottom", ImVec2(avail.x, content_h), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));   // normal spacing inside the debugger
        debugger.draw_content(ctx);
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    debugger.draw_strip(ctx);
}
    
}