#pragma once
#include <filesystem>
#include <string>

namespace lumen {

struct LaunchArgs
{
    bool game = false;
    bool pie = false;
    std::filesystem::path project;
    std::filesystem::path world;
    std::wstring live;

    static LaunchArgs parse_command_line();
};

}
