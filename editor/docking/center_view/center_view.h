#pragma once
#include <editor/docking/center_view/debugger.h>
#include <editor/docking/center_view/overlay_bar.h>
#include <editor/editor_context.h>
#include <editor/editor_camera.h>
#include <imgui.h>
#include <cstdint>

namespace lumen {

struct CenterView
{
    Debugger debugger;
    OverlayBar left_overlay;
    OverlayBar right_overlay;

    int selected_view = 0;
    const char* selected_image = "G_Albedo"; // Lit == albedo for now (swap to "Out_Color" once lighting exists)
    const char* selected_label = "Lit";
    float split_ratio = 0.66f;
    float screen_percentage = 1.0f;

    void initialize();
    
    bool _view_item(const char* p_icon, const char* p_label, const char* p_image, int p_id);
    void _draw_scene(EditorContext& ctx);
    void draw(EditorContext& ctx);
};

}