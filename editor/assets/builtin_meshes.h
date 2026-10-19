#pragma once
#include <core/assets/guid.h>
#include <filesystem>

namespace lumen {

struct BuiltinMeshes
{
    static constexpr Guid SPHERE = { 0x4C554D454E535048ull };
    static constexpr uint32_t SPHERE_REVISION = 1;
    static constexpr uint32_t SPHERE_SEGMENTS = 128;
    static constexpr uint32_t SPHERE_RINGS = 64;
    static constexpr float SPHERE_RADIUS = 0.5f;

    static std::filesystem::path sphere_bin();
    static bool ensure_sphere();
};

}
