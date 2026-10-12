#pragma once
#include <filesystem>

namespace lumen {

struct LaunchArgs
{
    bool game = false;
    bool pie = false;
    std::filesystem::path project;

    static LaunchArgs parse_command_line();
};

}
