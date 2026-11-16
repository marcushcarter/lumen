#include <editor/documents/terrain/terrain_document.h>
#include <editor/documents/terrain/panels/terrain_details_panel.h>
#include <editor/application/editor_context.h>
#include <editor/assets/import/terrain_cooker.h>
#include <core/terrain/terrain_source.h>
#include <cfloat>
#include <drivers/windows/dialogs_win32.h>
#include <cmath>
#include <vector>
#include <core/project/project.h>
#include <core/assets/asset_common.h>
#include <core/rendering/renderer.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <fstream>

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
    _frame_terrain(ctx);
    orbit.update();
    return OK;
}

void TerrainDocument::_frame_terrain(EditorContext& ctx)
{
    if (!has_data()) return;
    preview.terrain.terrain = terrain.guid;
    float low = terrain.height_min;
    float high = terrain.height_max;
    TerrainSource cooked;
    if (ctx.project && cooked.open(guid, ctx.project->content_path(guid)) == Error::OK) {
        low = FLT_MAX;
        high = -FLT_MAX;
        for (uint32_t t = 0; t < cooked.tile_count(); t++) {
            float lo, hi;
            cooked.node_range(t, 0, 0, 0, lo, hi);
            low = std::min(low, lo);
            high = std::max(high, hi);
        }
    }
    const glm::vec3 half(0.5f * (float)(terrain.samples_x - 1) * terrain.spacing, 0.5f * (high - low), 0.5f * (float)(terrain.samples_y - 1) * terrain.spacing);
    const float radius = glm::length(half);
    orbit.camera.near_z = std::max(orbit.camera.near_z, radius * NEAR_SCALE);
    orbit.pitch = FRAME_PITCH;
    orbit.frame(glm::vec3(0.0f, low + half.y, 0.0f), radius);
    orbit.camera.far_z = std::max(orbit.camera.far_z, orbit.max_distance + radius * 2.0f);
}

void TerrainDocument::_apply_new_data(EditorContext& ctx)
{
    (void)ctx;
    if (terrain.save(path) != Error::OK) log_write("Terrain: failed to save '%s'.", path.string().c_str());
    saved = terrain;
    _info_poll = -1.0e9;
    _info_time = {};
    preview.terrain.terrain = {};
    if (has_data()) {
        orbit.keep_view = false;
        _frame_terrain(ctx);
    }
}

void TerrainDocument::generate_flat(EditorContext& ctx)
{
    if (!ctx.project) return;
    if (has_data() && !drivers::Win32Dialogs::ask(L"Lumen Editor", L"Replace this terrain?", L"Generating replaces the current heights with flat ground. This can't be undone.", L"Generate", L"Cancel")) return;
    const uint32_t samples = GENERATE_SIZES[std::min<uint32_t>(generate_size, (uint32_t)std::size(GENERATE_SIZES) - 1)];
    const float range = generate_height_max - generate_height_min;
    if (range < MIN_HEIGHT_RANGE || generate_spacing < MIN_SPACING) return;
    const uint16_t flat = (uint16_t)std::clamp(std::lround(-generate_height_min / range * 65535.0f), 0l, 65535l);
    const std::vector<uint16_t> heights((size_t)samples * samples, flat);

    const std::filesystem::path bin = ctx.project->content_path(guid);
    if (ctx.renderer) ctx.renderer->terrain.reload(guid, bin);
    uint32_t tiles_x = 0, tiles_y = 0;
    if (TerrainCooker::write_cooked(bin, guid, heights.data(), samples, samples, generate_spacing, generate_height_min, generate_height_max, nullptr, tiles_x, tiles_y) != Error::OK) {
        log_write("Terrain: failed to generate '%s'.", path.string().c_str());
        return;
    }
    if (ctx.renderer) ctx.renderer->terrain.reload(guid, bin);

    terrain.source.clear();
    terrain.source_width = terrain.source_height = terrain.source_bits = 0;
    terrain.samples_x = terrain.samples_y = samples;
    terrain.tiles_x = tiles_x;
    terrain.tiles_y = tiles_y;
    terrain.spacing = generate_spacing;
    terrain.height_min = generate_height_min;
    terrain.height_max = generate_height_max;
    _apply_new_data(ctx);
}

void TerrainDocument::clear_terrain(EditorContext& ctx)
{
    if (!ctx.project || !has_data()) return;
    if (!drivers::Win32Dialogs::ask(L"Lumen Editor", L"Clear this terrain?", L"This deletes its heights. Worlds using it will show no terrain until you generate or import again.", L"Clear", L"Cancel")) return;
    if (ctx.renderer) ctx.renderer->terrain.reload(guid, ctx.project->content_path(guid));
    ctx.project->remove_content(guid);
    const Guid id = terrain.guid;
    terrain = LTerrain{};
    terrain.guid = id;
    _apply_new_data(ctx);
}

Error TerrainDocument::_write_header(EditorContext& ctx) const
{
    if (!ctx.project) return Error::FAILED;
    const std::filesystem::path bin = ctx.project->content_path(guid);
    if (ctx.renderer) ctx.renderer->terrain.reload(guid, bin);
    std::fstream f(bin, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) return Error::FAILED;
    const float values[3] = { terrain.spacing, terrain.height_min, terrain.height_max };
    f.seekp((std::streamoff)(sizeof(LAssetHeader) + offsetof(LTerrainPayloadHeader, spacing)));
    f.write(reinterpret_cast<const char*>(values), sizeof(values));
    return f ? Error::OK : Error::FAILED;
}

Error TerrainDocument::save(EditorContext& ctx)
{
    const bool header = terrain.samples_x && (terrain.spacing != saved.spacing || terrain.height_min != saved.height_min || terrain.height_max != saved.height_max);
    if (header && _write_header(ctx) != Error::OK) {
        log_write("Terrain: failed to update the cooked terrain for '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    if (terrain.save(path) != Error::OK) {
        log_write("Terrain: failed to save '%s'.", path.string().c_str());
        return Error::FAILED;
    }
    saved = terrain;
    _refresh_title();
    if (header) {
        orbit.keep_view = true;
        _frame_terrain(ctx);
    }
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
