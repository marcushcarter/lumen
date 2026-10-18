#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace lumen {

enum class ModelImportMode : uint8_t { FULL, MESH_ONLY };

const char* model_import_mode_name(ModelImportMode p_mode);
ModelImportMode model_import_mode_from_name(const std::string& p_name, ModelImportMode p_fallback);

struct LModel
{
    struct ImportRecord {
        std::string source;
        ModelImportMode mode = ModelImportMode::FULL;
    };

    Guid guid;
    Guid mesh;
    ImportRecord import;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;
};

}