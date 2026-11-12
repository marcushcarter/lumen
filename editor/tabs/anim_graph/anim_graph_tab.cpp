#include <editor/tabs/anim_graph/anim_graph_tab.h>
#include <editor/tabs/anim_graph/panels/anim_graph_canvas_panel.h>
#include <editor/tabs/anim_graph/panels/anim_graph_details_panel.h>
#include <editor/tabs/anim_graph/panels/anim_graph_preview_panel.h>

namespace lumen {

Error AnimGraphTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    if (graph.load(path) != OK || graph.guid != guid) {
        log_write("Animation Graph Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = graph;
    _refresh_title();

    _create_layout(ctx, "AnimGraphHost");
    layout.slots.clear();
    layout.add_panel<AnimGraphCanvasPanel>()->tab = this;
    layout.add_panel<AnimGraphDetailsPanel>()->tab = this;
    layout.add_panel<AnimGraphPreviewPanel>()->tab = this;
    layout.split_right = 1.0f - GRAPH_WIDTH;
    layout.center_split = 1.0f - BOTTOM_HEIGHT;

    _create_preview();
    orbit.update();
    return OK;
}

Error AnimGraphTab::save(EditorContext& ctx)
{
    (void)ctx;
    if (graph.save(path) != Error::OK) {
        log_write("Animation Graph Tab: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = graph;
    _refresh_title();
    return Error::OK;
}

void AnimGraphTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    _draw_layout(ctx);
    _update_orbit();
    orbit.update();
}

}
