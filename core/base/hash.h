#pragma once
#include <cstdint>
#include <string_view>

namespace lumen {

inline constexpr uint64_t HASH_FNV_OFFSET = 1469598103934665603ull;
inline constexpr uint64_t HASH_FNV_PRIME = 1099511628211ull;
inline constexpr uint32_t HASH_FNV_OFFSET_32 = 2166136261u;
inline constexpr uint32_t HASH_FNV_PRIME_32 = 16777619u;
inline constexpr uint64_t HASH_GOLDEN = 0x9E3779B97F4A7C15ull;

constexpr uint64_t hash_fnv_step(uint64_t p_hash, uint64_t p_value)
{
    return (p_hash ^ p_value) * HASH_FNV_PRIME;
}

constexpr uint64_t hash_fnv(std::string_view p_text, uint64_t p_hash = HASH_FNV_OFFSET)
{
    for (char c : p_text) p_hash = hash_fnv_step(p_hash, (uint8_t)c);
    return p_hash;
}

constexpr uint32_t hash_fnv32(std::string_view p_text)
{
    uint32_t h = HASH_FNV_OFFSET_32;
    for (char c : p_text) h = (h ^ (uint8_t)c) * HASH_FNV_PRIME_32;
    return h;
}

constexpr uint64_t hash_combine(uint64_t p_hash, uint64_t p_value)
{
    return p_hash ^ (p_value + HASH_GOLDEN + (p_hash << 6) + (p_hash >> 2));
}

constexpr uint64_t hash_splitmix(uint64_t p_value)
{
    uint64_t z = p_value + HASH_GOLDEN;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

}
