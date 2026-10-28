#pragma once
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <filesystem>
#include <string>

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

    bool operator==(const LMaterial&) const = default;
};

}
