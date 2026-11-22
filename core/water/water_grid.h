#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace lumen::water {

using namespace glm;

static constexpr int32_t TILE = 16;
static constexpr int32_t TILE_CELLS = TILE * TILE;
static constexpr int32_t SURFACE_ROW = TILE + 1;
static constexpr int32_t SURFACE_SAMPLES = SURFACE_ROW * SURFACE_ROW;
static constexpr int32_t SURFACE_MASK_WORDS = TILE_CELLS / 32;

enum Array : uint32_t { H, B, U, W, UT, WT, FLAGS, FLUX_U, FLUX_W, SCALE, FOAM, FOAM_OLD, UC, WC, CURL, FX, FZ, ARRAY_COUNT };
static constexpr uint32_t WIDE_COUNT = FOAM;
static constexpr uint32_t HALF_COUNT = ARRAY_COUNT - WIDE_COUNT;

static constexpr uint32_t CELL_KNOWN = 1u;
static constexpr uint32_t CELL_DRAIN = 2u;

static constexpr uint32_t PAGE_EMPTY = 0x00FFFFFFu;
static constexpr uint32_t PAGE_PHYS_MASK = 0x00FFFFFFu;
static constexpr uint32_t PAGE_ACTIVE = 1u << 30;
static constexpr uint32_t PAGE_AWAKE = 1u << 31;

static constexpr float EPS = 0.004f;
static constexpr float WET = 0.01f;
static constexpr float SURFACE_WET = 0.01f;
static constexpr float WALL = 1.0e6f;
static constexpr float WALL_TEST = 0.5e6f;
static constexpr float FOAM_MAX = 1.5f;
static constexpr float SHORE_TOLERANCE = 0.02f;
static constexpr float HIDE_DEPTH = 0.08f;
static constexpr float FALL_SLOPE = 2.0f;
static constexpr float FALL_MIN = 0.3f;
static constexpr int32_t SHORE_REACH = 2;
static constexpr int32_t EXT_REACH = 4;
static constexpr uint32_t SURFACE_FRESH = 0x8000u;

static constexpr float SAMPLE_INVALID = 0.0f;
static constexpr float SAMPLE_HIDDEN = 1.0f;
static constexpr float SAMPLE_SHORE = 2.0f;
static constexpr float SAMPLE_WET = 3.0f;

inline int32_t floor_div(int32_t p_a, int32_t p_b) { return p_a >= 0 ? p_a / p_b : -((-p_a + p_b - 1) / p_b); }
inline int32_t wrap(int32_t p_a, int32_t p_m) { const int32_t r = p_a % p_m; return r < 0 ? r + p_m : r; }
inline uint64_t tile_key(ivec2 p_tile) { return ((uint64_t)(uint32_t)p_tile.x << 32) | (uint32_t)p_tile.y; }
inline ivec2 key_tile(uint64_t p_key) { return ivec2((int32_t)(uint32_t)(p_key >> 32), (int32_t)(uint32_t)(p_key & 0xFFFFFFFFull)); }
inline uint32_t pack_tile(ivec2 p_local) { return (uint32_t)(p_local.x + 1) | ((uint32_t)(p_local.y + 1) << 16); }
inline ivec2 unpack_tile(uint32_t p_packed) { return ivec2((int32_t)(p_packed & 0x7FFFu) - 1, (int32_t)((p_packed >> 16) & 0x7FFFu) - 1); }

}
