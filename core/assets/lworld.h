#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <filesystem>

namespace lumen {

struct LWorld
{
    Guid guid;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;

    bool operator==(const LWorld&) const = default;
};

}
