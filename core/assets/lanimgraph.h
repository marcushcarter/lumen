#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <filesystem>

namespace lumen {

struct LAnimGraph
{
    Guid guid;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;

    bool operator==(const LAnimGraph&) const = default;
};

}
