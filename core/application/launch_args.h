#pragma once
#include <filesystem>

namespace lumen {

struct LaunchArgs
{
    bool game = false;
    bool pie = false;
    bool debug = false;
    std::filesystem::path project;
    std::filesystem::path world;

    static LaunchArgs parse_command_line();
};

}
