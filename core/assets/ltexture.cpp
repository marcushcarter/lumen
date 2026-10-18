#include <core/assets/ltexture.h>

namespace lumen {

const char* texture_usage_name(TextureUsage p_usage)
{
    switch (p_usage) {
        case TextureUsage::COLOR: return "color";
        case TextureUsage::NORMAL: return "normal";
        case TextureUsage::MASK: return "mask";
        case TextureUsage::LINEAR: return "linear";
    }
    return "color";
}

TextureUsage texture_usage_from_name(const std::string& p_name, TextureUsage p_fallback)
{
    if (p_name == "color") return TextureUsage::COLOR;
    if (p_name == "normal") return TextureUsage::NORMAL;
    if (p_name == "mask") return TextureUsage::MASK;
    if (p_name == "linear") return TextureUsage::LINEAR;
    return p_fallback;
}

const char* texture_quality_name(TextureQuality p_quality)
{
    switch (p_quality) {
        case TextureQuality::FAST: return "fast";
        case TextureQuality::NORMAL: return "normal";
        case TextureQuality::BEST: return "best";
    }
    return "best";
}

TextureQuality texture_quality_from_name(const std::string& p_name, TextureQuality p_fallback)
{
    if (p_name == "fast") return TextureQuality::FAST;
    if (p_name == "normal") return TextureQuality::NORMAL;
    if (p_name == "best") return TextureQuality::BEST;
    return p_fallback;
}

}