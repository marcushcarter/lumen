#include <core/assets/guid.h>
#include <core/base/hash.h>
#include <chrono>
#include <random>
#include <thread>
#include <cstring>

namespace lumen {

static constexpr char HEX[] = "0123456789abcdef";

static inline uint64_t _splitmix64(uint64_t& r_state)
{
    const uint64_t z = hash_splitmix(r_state);
    r_state += HASH_GOLDEN;
    return z;
}

static uint64_t& _generator_state()
{
    thread_local uint64_t state = []() -> uint64_t {
        std::random_device rd;
        uint64_t s = ((uint64_t)rd() << 32) | (uint64_t)rd();
        s ^= (uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
        s ^= (uint64_t)std::hash<std::thread::id>{}(std::this_thread::get_id()) * 0x9e3779b97f4a7c15ull;
        return s;
    }();
    return state;
}

static inline int _hex_value(char p_c)
{
    if (p_c >= '0' && p_c <= '9') return p_c - '0';
    if (p_c >= 'a' && p_c <= 'f') return p_c - 'a' + 10;
    if (p_c >= 'A' && p_c <= 'F') return p_c - 'A' + 10;
    return -1;
}

static inline void _write_hex64(char* p_out, uint64_t p_value)
{
    for (int i = 15; i >= 0; --i) {
        p_out[i] = HEX[p_value & 0xf];
        p_value >>= 4;
    }
}

Guid Guid::generate()
{
    uint64_t& state = _generator_state();
    uint64_t v;
    do { v = _splitmix64(state); } while (v == INVALID);
    return Guid{ v };
}

Guid Guid::from_string(std::string_view p_text)
{
    if (p_text.size() != CHARS) return Guid{};
    uint64_t v = 0;
    for (char c : p_text) {
        int d = _hex_value(c);
        if (d < 0) return Guid{};
        v = (v << 4) | (uint64_t)d;
    }
    return Guid{ v };
}

void Guid::to_chars(char* r_out) const
{
    _write_hex64(r_out, value);
    r_out[CHARS] = '\0';
}

void Guid::to_path_chars(char* r_out) const
{
    r_out[0] = HEX[(value >> 60) & 0xf];
    r_out[1] = HEX[(value >> 56) & 0xf];
    r_out[2] = '/';
    _write_hex64(r_out + 3, value);
    r_out[PATH_CHARS] = '\0';
}

std::string Guid::to_string() const
{
    char buf[BUFFER];
    to_chars(buf);
    return std::string(buf, CHARS);
}

}
