#ifndef WATER_GRID_GLSL
#define WATER_GRID_GLSL

#include "water/common.glsl"

struct WaterData {
    WaterFloats state;
    WaterHalfs state16;
    WaterUints page;
    ivec2 origin;
    int slots;
    int tiles_w;
    int n;
    uint cells;
    float dx;
};

WaterData water_data(WaterLevel p_l) {
    WaterData d;
    d.state = p_l.state;
    d.state16 = p_l.state16;
    d.page = p_l.page;
    d.origin = p_l.origin;
    d.slots = p_l.slots;
    d.tiles_w = p_l.tiles_w;
    d.n = p_l.n;
    d.cells = p_l.cells;
    d.dx = p_l.dx;
    return d;
}

int water_slot(WaterData p_d, int p_tx, int p_ty) {
    return water_wrap(p_d.origin.y + p_ty, p_d.slots) * p_d.slots + water_wrap(p_d.origin.x + p_tx, p_d.slots);
}

uint water_page_global(WaterData p_d, int p_tx, int p_ty) {
    if (p_tx < -1 || p_ty < -1 || p_tx > p_d.tiles_w || p_ty > p_d.tiles_w) return PAGE_EMPTY;
    return p_d.page.v[water_wrap(p_d.origin.y + p_ty, p_d.slots) * p_d.slots + water_wrap(p_d.origin.x + p_tx, p_d.slots)];
}

int water_phys_cell(uint p_page, int p_i, int p_j) {
    const uint phys = p_page & PAGE_PHYS;
    if (phys == PAGE_EMPTY) return -1;
    return int((phys << 8u) + uint(((p_j & 15) << 4) + (p_i & 15)));
}

int water_cell_global(WaterData p_d, int p_i, int p_j) {
    return water_phys_cell(water_page_global(p_d, p_i >> 4, p_j >> 4), p_i, p_j);
}

float dget(WaterData p_d, uint p_a, int p_c) {
    if (p_c < 0) return water_default(p_a);
    if (p_a < A_WIDE) return p_d.state.v[p_a * p_d.cells + uint(p_c)];
    return float(p_d.state16.v[(p_a - A_WIDE) * p_d.cells + uint(p_c)]);
}

void dput(WaterData p_d, uint p_a, int p_c, float p_x) {
    if (p_c < 0) return;
    if (p_a < A_WIDE) p_d.state.v[p_a * p_d.cells + uint(p_c)] = p_x;
    else p_d.state16.v[(p_a - A_WIDE) * p_d.cells + uint(p_c)] = float16_t(p_x);
}


shared uint s_page[9];
ivec2 g_center = ivec2(-1000000);

void water_cache_pages(WaterData p_d, ivec2 p_center) {
    const uint lid = gl_LocalInvocationIndex;
    if (lid < 9u) s_page[lid] = water_page_global(p_d, p_center.x + int(lid % 3u) - 1, p_center.y + int(lid / 3u) - 1);
    g_center = p_center;
    barrier();
}

uint water_page_cached(WaterData p_d, int p_tx, int p_ty) {
    const ivec2 o = ivec2(p_tx, p_ty) - g_center + 1;
    if (o.x >= 0 && o.y >= 0 && o.x < 3 && o.y < 3) return s_page[o.y * 3 + o.x];
    return water_page_global(p_d, p_tx, p_ty);
}

int water_cell_cached(WaterData p_d, int p_i, int p_j) {
    return water_phys_cell(water_page_cached(p_d, p_i >> 4, p_j >> 4), p_i, p_j);
}

#endif
