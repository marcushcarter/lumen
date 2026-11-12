#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace lumen {

struct BlendSample
{
    Guid clip;
    float position = 0.0f;
    float direction = 0.0f;

    bool operator==(const BlendSample&) const = default;
};

struct LBlendSpace
{
    Guid guid;
    Guid preview_mesh;
    std::string axis = "speed";
    std::vector<BlendSample> samples;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;

    bool operator==(const LBlendSpace&) const = default;
};

}
