#pragma once
#include <cstdint>

namespace lumen {

enum class LiveMessage : uint8_t {
    WORLD = 1,
    ENTITY = 2,
    DESTROY = 3,
    ENVIRONMENT = 4,
};

struct LiveHeader
{
    uint32_t size = 0;
    LiveMessage type = LiveMessage::WORLD;
    uint8_t pad[3] = {};
};

static_assert(sizeof(LiveHeader) == 8, "LiveHeader layout changed");

inline constexpr const wchar_t* LIVE_PIPE_PREFIX = L"\\\\.\\pipe\\lumen_live_";
inline constexpr uint32_t LIVE_PIPE_BUFFER = 4u * 1024 * 1024;
inline constexpr uint32_t LIVE_MESSAGE_MAX = 64u * 1024 * 1024;

}
