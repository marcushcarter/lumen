#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

namespace lumen {

enum class ModelImportMode : uint8_t { FULL, MESH_ONLY };

const char* model_import_mode_name(ModelImportMode p_mode);
ModelImportMode model_import_mode_from_name(const std::string& p_name, ModelImportMode p_fallback);

struct LModelPayloadHeader
{
    Guid mesh;
    uint32_t material_count;
    uint32_t _pad;
};

static_assert(sizeof(LModelPayloadHeader) == 16, "LModelPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LModelPayloadHeader>, "must be blittable");

struct LModel
{
    struct ImportRecord {
        std::string source;
        ModelImportMode mode = ModelImportMode::FULL;
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
