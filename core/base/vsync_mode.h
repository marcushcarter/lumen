#pragma once
#include <cstdint>
#include <string_view>

namespace lumen {

enum class VsyncMode : uint8_t { OFF, FAST, ON };

inline constexpr const char* VSYNC_MODE_NAMES[] = { "Off", "Fast", "On" };

inline const char* vsync_mode_name(VsyncMode p_mode)
{
    return VSYNC_MODE_NAMES[(int)p_mode];
}

inline VsyncMode vsync_mode_from_name(std::string_view p_name, VsyncMode p_fallback)
{
    for (int i = 0; i < 3; i++) if (p_name == VSYNC_MODE_NAMES[i]) return (VsyncMode)i;
    return p_fallback;
}

}