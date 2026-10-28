#pragma once
#include <core/base/enum_flags.h>
#include <cstdint>
#include <string>
#include <type_traits>

namespace lumen {

enum class TextureUsage : uint8_t { COLOR, NORMAL, MASK, LINEAR };
enum class TextureQuality : uint8_t { FAST, NORMAL, BEST };

const char* texture_usage_name(TextureUsage p_usage);
TextureUsage texture_usage_from_name(const std::string& p_name, TextureUsage p_fallback);
const char* texture_quality_name(TextureQuality p_quality);
TextureQuality texture_quality_from_name(const std::string& p_name, TextureQuality p_fallback);

enum class TextureFlags : uint32_t { NONE = 0, SRGB = 1u << 0, NORMAL_MAP = 1u << 1 };
LUMEN_ENUM_FLAGS(TextureFlags)

struct LTexturePayloadHeader
{
    uint32_t vk_format;
    uint32_t width;
    uint32_t height;
    uint32_t mip_count;
    uint32_t flags;
    uint32_t _pad;
};

static_assert(sizeof(LTexturePayloadHeader) == 24, "LTexturePayloadHeader layout changed");
static_assert(std::is_trivially_copyable_v<LTexturePayloadHeader>, "must be blittable");

}
