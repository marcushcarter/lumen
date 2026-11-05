#pragma once
#include <core/application/live_protocol.h>
#include <core/assets/guid.h>
#include <core/base/error.h>
#include <cstddef>
#include <string>
#include <unordered_map>

namespace lumen {

struct World;

struct LiveLink
{
    static constexpr double POLL_S = 1.0 / 30.0;

    void* pipe = nullptr;
    void* overlapped = nullptr;
    std::wstring name;
    bool connecting = false;
    bool connected = false;

    Guid world;
    std::unordered_map<uint64_t, uint64_t> sent;
    std::unordered_map<uint64_t, uint64_t> _current;
    uint64_t sent_environment = 0;
    double poll = -1.0e9;
    uint32_t _sequence = 0;

    Error open();
    void close();
    bool is_open() const { return pipe != nullptr; }
    void baseline(World& r_world, Guid p_world);
    void tick(World& r_world, Guid p_world, double p_now);

    void _poll_connect();
    void _snapshot(World& r_world, std::unordered_map<uint64_t, uint64_t>& r_hashes);
    bool _send(LiveMessage p_type, const void* p_data, size_t p_size);
};

}
