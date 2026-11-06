#include <editor/tabs/terrain/terrain_tab.h>
#include <editor/tabs/terrain/panels/terrain_details_panel.h>

namespace lumen {

Error TerrainTab::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    if (terrain.load(path) != OK || terrain.guid != guid) {
        log_write("Terrain Tab: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = terrain;
    _refresh_title();

    _create_layout(ctx, "TerrainHost");
    layout.add_panel<TerrainDetailsPanel>()->tab = this;
    _create_preview();
    orbit.update();
    return OK;
}

Error TerrainTab::save(EditorContext& ctx)
{
    (void)ctx;
    if (terrain.save(path) != Error::OK) {
        log_write("Terrain Tab: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = terrain;
    _refresh_title();
    return Error::OK;
}

void TerrainTab::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    _draw_layout(ctx);
    _update_orbit();
    orbit.update();
}

}
