#pragma once
#include <cstdint>
#include <cstring>

namespace lumen {

inline constexpr const char* PASS_CATEGORY_UPLOAD = "Upload";
inline constexpr const char* PASS_CATEGORY_CULLING = "Culling";
inline constexpr const char* PASS_CATEGORY_RASTER = "Raster";
inline constexpr const char* PASS_CATEGORY_MATERIAL = "Material";
inline constexpr const char* PASS_CATEGORY_SHADOWS = "Shadows";
inline constexpr const char* PASS_CATEGORY_LIGHTING = "Lighting";
inline constexpr const char* PASS_CATEGORY_GI = "GI";
inline constexpr const char* PASS_CATEGORY_EFFECTS = "Effects";
inline constexpr const char* PASS_CATEGORY_POST = "Post";
inline constexpr const char* PASS_CATEGORY_UI = "UI";
inline constexpr const char* PASS_CATEGORY_PRESENT = "Present";
inline constexpr const char* PASS_CATEGORY_EDITOR = "Editor";
inline constexpr const char* PASS_CATEGORY_PROFILING = "Profiling";

struct PassCategory {
    const char* name;
    uint8_t r, g, b;
};

inline constexpr PassCategory PASS_CATEGORIES[] = {
    { PASS_CATEGORY_UPLOAD, 150, 128, 104 },
    { PASS_CATEGORY_CULLING, 86, 156, 240 },
    { PASS_CATEGORY_RASTER, 98, 196, 120 },
    { PASS_CATEGORY_MATERIAL, 240, 160, 72 },
    { PASS_CATEGORY_SHADOWS, 120, 112, 224 },
    { PASS_CATEGORY_LIGHTING, 236, 212, 84 },
    { PASS_CATEGORY_GI, 64, 196, 184 },
    { PASS_CATEGORY_EFFECTS, 224, 96, 200 },
    { PASS_CATEGORY_POST, 236, 104, 96 },
    { PASS_CATEGORY_UI, 168, 184, 212 },
    { PASS_CATEGORY_PRESENT, 112, 160, 168 },
    { PASS_CATEGORY_EDITOR, 150, 120, 190 },
    { PASS_CATEGORY_PROFILING, 130, 130, 130 },
};

inline const PassCategory* pass_category_find(const char* p_name) {
    for (const PassCategory& c : PASS_CATEGORIES) if (std::strcmp(c.name, p_name) == 0) return &c;
    return nullptr;
}

}
