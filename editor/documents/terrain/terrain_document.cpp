#include <editor/documents/terrain/terrain_document.h>
#include <editor/documents/terrain/panels/terrain_details_panel.h>

namespace lumen {

Error TerrainDocument::create(EditorContext& ctx)
{
    using enum Error;
    if (!_resolve_path(ctx)) return FAILED;
    if (terrain.load(path) != OK || terrain.guid != guid) {
        log_write("Terrain: failed to load '%s'.", path.string().c_str());
        return FAILED;
    }
    saved = terrain;
    _refresh_title();

    _create_layout();
    layout.add_panel<TerrainDetailsPanel>()->tab = this;
    _create_preview();
    orbit.update();
    return OK;
}

Error TerrainDocument::save(EditorContext& ctx)
{
    (void)ctx;
    if (terrain.save(path) != Error::OK) {
        log_write("Terrain: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = terrain;
    _refresh_title();
    return Error::OK;
}

void TerrainDocument::on_update(EditorContext& ctx, float p_dt)
{
    (void)p_dt;
    _draw_layout(ctx);
    _update_orbit();
    orbit.update();
}

}
