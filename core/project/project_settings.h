#pragma once
#include <core/base/vsync_mode.h>
#include <core/assets/guid.h>
#include <cstdint>
#include <string>
#include <filesystem>

namespace lumen {

struct ProjectSettings
{
    static constexpr int DEFAULT_WIDTH = 1280;
    static constexpr int DEFAULT_HEIGHT = 720;

    int width = DEFAULT_WIDTH;
    int height = DEFAULT_HEIGHT;
    Guid window_icon;
    std::string window_title;

    friend bool operator==(const ProjectSettings&, const ProjectSettings&) = default;
};
    
}