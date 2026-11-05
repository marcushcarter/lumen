#pragma once
#include <core/animation/anim_mask.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstdint>
#include <filesystem>
#include <string>

namespace lumen {

enum class OverlayMode : uint8_t { OVERRIDE, ADDITIVE };

const char* overlay_mode_name(OverlayMode p_mode);
OverlayMode overlay_mode_from_name(const std::string& p_name, OverlayMode p_fallback);

struct LOverlay
{
    Guid guid;
    Guid source;
    Guid reference;
    Guid preview_blend_space;
    OverlayMode mode = OverlayMode::OVERRIDE;
    float fade = 0.2f;
    AnimMask mask;

    Error load(const std::filesystem::path& p_path);
    Error save(const std::filesystem::path& p_path) const;

    bool operator==(const LOverlay&) const = default;
};

}
