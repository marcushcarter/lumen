#ifndef SUN_CASCADE_KIND_GLSL
#define SUN_CASCADE_KIND_GLSL

#include "common/geometry.glsl"
#include "common/compact_draw.glsl"
#include "lighting/sun_cascade_common.glsl"

uint sun_cascade_kind(uint p_entry, bool p_mixed) {
    const uint bin = slot_bin(p_entry);
    if ((bin & RASTER_BIN_MASKED) != 0u) return p_mixed ? SUN_CASCADE_MIXED : SUN_CASCADE_MASKED;
    if ((bin & RASTER_BIN_TWO_SIDED) != 0u) return SUN_CASCADE_TWO_SIDED;
    return SUN_CASCADE_OPAQUE;
}

#endif
