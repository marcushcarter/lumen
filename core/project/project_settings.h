#pragma once
#include <core/base/vsync_mode.h>
#include <cstdint>
#include <string>
#include <filesystem>


namespace lumen {

struct ProjectSettings
{
    // std::string description;
    // std::string version "1.0.0";
    // std::filesystem::path icon;
    // std::filesystem::path windows_icon;

    int width = 1280, height = 720;
    // int init_x = -1, init_y = -1;
    // int vsync_mode = 1;
};
    
}