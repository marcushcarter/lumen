#pragma once
#include <core/base/error.h>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen {

struct World;

struct LiveClient
{
    void* pipe = nullptr;
    std::vector<uint8_t> buffer;

    Error connect(const std::wstring& p_name);
    void close();
    bool is_open() const { return pipe != nullptr; }
    void tick(World& r_world);

    void _apply(World& r_world, uint8_t p_type, const uint8_t* p_data, uint32_t p_size);
};

}
