#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>

namespace lumen {

enum class AlphaMode : uint8_t { NONE, MASKED, BLEND };

const char* alpha_mode_name(AlphaMode p_mode);
AlphaMode alpha_mode_from_name(const std::string& p_name, AlphaMode p_fallback);

struct LMaterialPayloadHeader
{
    Guid mask_texture;
    float alpha_cutoff;
    uint8_t alpha_mode;
    uint8_t two_sided;
    uint8_t _pad[2];
};

static_assert(sizeof(LMaterialPayloadHeader) == 16, "LMaterialPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LMaterialPayloadHeader>, "must be blittable");

struct LMaterial
{
    struct ImportRecord {
        std::string source;
        std::string name;

        bool operator==(const ImportRecord&) const = default;
    };

    Guid guid;

    AlphaMode alpha_mode = AlphaMode::NONE;
    bool two_sided = false;
    Guid mask_texture;
    float alpha_cutoff = 0.5f;
    
    ImportRecord import;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;
    Error load_cooked(const std::filesystem::path& p_path);
    Error save_cooked(const std::filesystem::path& p_path) const;

    bool operator==(const LMaterial&) const = default;
};

}
