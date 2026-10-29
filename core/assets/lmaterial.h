#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <filesystem>
#include <string>
#include <type_traits>

namespace lumen {

using namespace glm;

enum class ShadingModel : uint8_t { SKY, DEFAULT_LIT, UNLIT, FOLIAGE, SKIN, CLOTH, HAIR, EYE };
enum class AlphaMode : uint8_t { NONE, MASKED };

const char* shading_model_name(ShadingModel p_model);
ShadingModel shading_model_from_name(const std::string& p_name, ShadingModel p_fallback);
const char* alpha_mode_name(AlphaMode p_mode);
AlphaMode alpha_mode_from_name(const std::string& p_name, AlphaMode p_fallback);

struct LMaterial
{
    struct ImportRecord {
        std::string source;
        std::string name;

        bool operator==(const ImportRecord&) const = default;
    };

    struct Cooked {
        vec4 base_color;
        vec4 custom;
        vec3 emissive_color;
        float emissive_nits;
        vec2 uv_scale;
        vec2 uv_offset;
        Guid base_color_texture;
        Guid normal_texture;
        Guid roughness_texture;
        Guid metallic_texture;
        Guid ao_texture;
        Guid emissive_texture;
        Guid custom_texture;
        float alpha_cutoff;
        float normal_scale;
        float roughness;
        float metallic;
        float ao_strength;
        uint8_t shading_model;
        uint8_t alpha_mode;
        uint8_t two_sided;
        uint8_t _pad;
    };

    Guid guid;

    ShadingModel shading_model = ShadingModel::DEFAULT_LIT;
    AlphaMode alpha_mode = AlphaMode::NONE;
    bool two_sided = false;
    float alpha_cutoff = 0.5f;

    vec4 base_color = vec4(1.0f);
    Guid base_color_texture;

    Guid normal_texture;
    float normal_scale = 1.0f;

    float roughness = 1.0f;
    Guid roughness_texture;
    float metallic = 0.0f;
    Guid metallic_texture;
    float ao_strength = 1.0f;
    Guid ao_texture;

    vec3 emissive_color = vec3(1.0f);
    float emissive_nits = 0.0f;
    Guid emissive_texture;

    vec4 custom = vec4(0.0f);
    Guid custom_texture;

    vec2 uv_scale = vec2(1.0f);
    vec2 uv_offset = vec2(0.0f);

    ImportRecord import;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;
    Error load_cooked(const std::filesystem::path& p_path);
    Error save_cooked(const std::filesystem::path& p_path) const;

    bool operator==(const LMaterial&) const = default;
};

static_assert(sizeof(LMaterial::Cooked) == 144, "LMaterial::Cooked layout changed");
static_assert(std::is_trivially_copyable_v<LMaterial::Cooked>);

}
