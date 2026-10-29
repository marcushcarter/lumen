#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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

    struct Cooked {
        Guid mesh;
        uint32_t material_count = 0;
        uint32_t _pad = 0;
    };

    Guid guid;
    Guid mesh;
    std::vector<Guid> materials;
    ImportRecord import;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;
    Error load_cooked(const std::filesystem::path& p_path);
    Error save_cooked(const std::filesystem::path& p_path) const;
};

}
