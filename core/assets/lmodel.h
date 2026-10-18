#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <filesystem>

namespace lumen {

struct LModel
{
    Guid guid;
    Guid mesh;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;
};

}