#pragma once
#include <core/assets/asset_common.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <glm/vec3.hpp>
#include <string>
#include <type_traits>

namespace lumen {

enum class AlphaMode : uint8_t { NONE, MASKED, BLEND };

const char* alpha_mode_name(AlphaMode p_mode);
AlphaMode alpha_mode_from_name(const std::string& p_name, AlphaMode p_fallback);

struct LMaterialPayloadHeader
{
    Guid base_color_texture;
    Guid normal_texture;
    Guid mask_texture;
    float alpha_cutoff;
    float color[3];
    uint8_t alpha_mode;
    uint8_t two_sided;
    uint8_t mask_channel;
    uint8_t _pad[5];
};

static_assert(sizeof(LMaterialPayloadHeader) == 48, "LMaterialPayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LMaterialPayloadHeader>, "must be blittable");

struct LMaterial
{
    struct ImportRecord {
        std::string source;
        std::string name;

        bool operator==(const ImportRecord&) const = default;
    };

    Guid guid;

    glm::vec3 color = glm::vec3(1.0f);
    Guid base_color_texture;
    Guid normal_texture;
    AlphaMode alpha_mode = AlphaMode::NONE;
    bool two_sided = false;
    Guid mask_texture;
    TextureChannel mask_channel = TextureChannel::A;
    float alpha_cutoff = 0.5f;
    
    ImportRecord import;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;
    Error load_cooked(const std::filesystem::path& p_path);
    Error save_cooked(const std::filesystem::path& p_path) const;

    bool operator==(const LMaterial&) const = default;
};

}
